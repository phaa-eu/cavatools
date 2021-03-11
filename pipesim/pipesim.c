/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "cache.h"
#include "perfctr.h"
#include "pipesim.h"

#include "lru_fsm_1way.h"
#include "lru_fsm_2way.h"
#include "lru_fsm_3way.h"
#include "lru_fsm_4way.h"

static const char *in_path, *out_path, *perf_path;

long load_latency, fma_latency, branch_delay;

#define mpy_cycles   8
#define div_cycles  32
#define fma_div_cycles (fma_latency*3)


const struct options_t opt[] =
  {
   { "--in=s",		.s=&in_path,		.ds=0,	.h="Trace file from caveat =name" },
   { "--perf=s",	.s=&perf_path,		.ds=0,	.h="Performance counters in shared memory =name" },
     
   { "--load=i",	.i=&load_latency,	.di=6,	.h="Load latency from cache" },
   { "--fma=i",		.i=&fma_latency,	.di=2,	.h="fused multiply add unit latency" },
   
   { "--imiss=i",	.i=&ic.penalty,		.di=12,	.h="I$ miss latency is =number cycles" },
   { "--iline=i",	.i=&ic.lg_line,		.di=5,	.h="I$ line size is 2^ =n bytes" },
   { "--iways=i",	.i=&ic.ways,		.di=4,	.h="I$ is =w ways set associativity" },
   { "--isets=i",	.i=&ic.lg_rows,		.di=7,	.h="I$ has 2^ =n sets per way" },
   
   { "--dmiss=i",	.i=&dc.penalty,		.di=12,	.h="D$ miss latency is =number cycles" },
   { "--dline=i",	.i=&dc.lg_line,		.di=5,	.h="D$ line size is 2^ =n bytes" },
   { "--dways=i",	.i=&dc.ways,		.di=4,	.h="D$ is =w ways set associativity" },
   { "--dsets=i",	.i=&dc.lg_rows,		.di=7,	.h="D$ has 2^ =n sets per way" },

   { "--bdelay=i",	.i=&branch_delay,	.di=2,	.h="Taken branch delay is =number cycles" },
   
   { "--out=s",		.s=&out_path,		.ds=0,	.h="Create output trace file =name" },
   { "--report=i",	.i=&report,    		.di=100,.h="Progress report every =number million instructions" },
   { "--quiet",		.b=&quiet,		.bv=1,	.h="Don't report progress to stderr" },
   { "-q",		.b=&quiet,		.bv=1,	.h="short for --quiet" },
   { 0 }
  };
const char* usage = "pipesim --in=trace --perf=counters [pipesim-options] target-program";

long quiet, report;
struct timeval start_time;
long instructions_executed, cycles_simulated;
long ibuf_misses;

struct cache_t ic, dc;
struct fifo_t* in;
struct fifo_t* out;
int hart;
uint64_t mem_queue[tr_memq_len];


struct lru_fsm_t* choose_fsm(int ways)
{
  switch (ways) {
  case 1:  return cache_fsm_1way;
  case 2:  return cache_fsm_2way;
  case 3:  return cache_fsm_3way;
  case 4:  return cache_fsm_4way;
  default:  fprintf(stderr, "--ways=1..4 only\n");  exit(-1);
  }
}

int main(int argc, const char** argv)
{
  assert(sizeof(struct insn_t) == 8);
  gettimeofday(&start_time, 0);

  for (int i=0; i<Number_of_opcodes; i++) {
    unsigned int a = insnAttr[i].flags;
    insnAttr[i].latency = (attr_l & a ? load_latency :
			   attr_M & a ? mpy_cycles :
			   attr_D & a ? div_cycles :
			   attr_E & a ? fma_div_cycles :
			   attr_f & a ? fma_latency : /* note after other FP flags */
			   1);
  }
  
  int numopts = parse_options(argv+1);
  if (argc == numopts+1 || !in_path)
    help_exit();

  long entry = load_elf_binary(argv[1+numopts], 0);
  insnSpace_init();

  report *= 1000000;
  in = fifo_open(in_path);
  if (out_path)
    out = fifo_create(out_path, 0);
  if (perf_path) {
    perf_create(perf_path);
    perf.start = start_time;
  }
  
  init_cache(&ic,choose_fsm(ic.ways), 0);
  init_cache(&dc,choose_fsm(dc.ways), 1);

  show_cache(&ic, "I$", 1, stdout);
  show_cache(&dc, "D$", 1, stdout);

  simulate(report);
  if (out_path) {
    fifo_put(out, trM(tr_eof, 0));
    fifo_finish(out);
  }
  if (perf_path)
    perf_close(&perf);
  fifo_close(in);
  
  fprintf(stderr, "\n\n");
  fprintf(stdout, "%12ld instructions executed\n", instructions_executed);
  fprintf(stdout, "%12ld cycles simulated\n", cycles_simulated);
  fprintf(stdout, "IPC = %12.3f\n", (double)instructions_executed/cycles_simulated);

  show_cache(&ic, "I$", instructions_executed, stdout);
  show_cache(&dc, "I$", instructions_executed, stdout);
  return 0;
}



void status_report(long now, long icount, long ibmisses)
{
  instructions_executed = icount;
  ibuf_misses = ibmisses;
  cycles_simulated = now;
  struct timeval this_time;
  gettimeofday(&this_time, 0);
  double msec = (this_time.tv_sec - start_time.tv_sec)*1000;
  msec += (this_time.tv_usec - start_time.tv_usec)/1000.0;
  fprintf(stderr, "\r%3.1fBi %3.1fBc IPC=%5.3f CPS=%5.3f in %lds",
	  icount/1e9, now/1e9, (double)icount/now, now/(1e3*msec), (long)(msec/1e3));
  if (perf_path) {
    perf.h->insns = icount;
    perf.h->cycles = now;
    perf.h->ib_misses = ibmisses;
    perf.h->dc_misses = dc.misses;
    double kinsns = icount/1e3;
    fprintf(stderr, " IC$=%4.2f DC$=%4.2f m/Ki", ibmisses/kinsns, dc.misses/kinsns);
  }
}

