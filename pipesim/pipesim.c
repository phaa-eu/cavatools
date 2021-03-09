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

static const char *in_path, *out_path, *perf_path, *wflag;
long load_latency, fma_latency, branch_delay;

const struct options_t opt[] =
  {
   { "--in=s",		.s=&in_path,		.ds=0,		.h="Trace file from caveat =name" },
   { "--perf=s",	.s=&perf_path,		.ds=0,		.h="Performance counters in shared memory =name" },
     
   { "--load=i",	.i=&load_latency,	.di=6,		.h="Load latency from cache" },
   { "--fma=i",		.i=&fma_latency,	.di=2,		.h="fused multiply add unit latency" },
   
   { "--miss=i",	.i=&sc.penalty,		.di=200,	.h="Cache miss latency is =number cycles" },
   { "--line=i",	.i=&sc.lg_line,		.di=8,		.h="Cache line size is 2^ =n bytes" },
   { "--ways=i",	.i=&sc.ways,		.di=4,		.h="Cache is =w ways set associativity" },
   { "--sets=i",	.i=&sc.lg_rows,		.di=10,		.h="Cache has 2^ =n sets per way" },
   { "--write=s",	.s=&wflag,		.ds="b",	.h="Cache is write=[back|thru]" },

   { "--bdelay=i",	.i=&branch_delay,	.di=0,		.h="Taken branch delay is =number cycles" },
   
   { "--out=s",		.s=&out_path,		.ds=0,		.h="Create output trace file =name" },
   { "--report=i",	.i=&report,    		.di=100,	.h="Progress report every =number million instructions" },
   { "--quiet",		.b=&quiet,		.bv=1,		.h="Don't report progress to stderr" },
   { "-q",		.b=&quiet,		.bv=1,		.h="short for --quiet" },
   { 0 }
  };
const char* usage = "pipesim --in=trace --perf=counters [pipesim-options] target-program";

long quiet, report;
struct timeval start_time;
long instructions_executed, cycles_simulated;
long ibuf_misses;

struct cache_t sc;
struct fifo_t* in;
struct fifo_t* out;
int hart;
uint64_t mem_queue[tr_memq_len];


#define mpy_cycles   8
#define div_cycles  32
#define fma_div_cycles (fma_latency*3)


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
  
  /* initialize cache */
  struct lru_fsm_t* fsm;
  switch (sc.ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--dways=1..4 only\n");  exit(-1);
  }
  init_cache(&sc,fsm, !(wflag && wflag[0]=='t'));

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
  fprintf(stdout, "%12.3f IPC\n", (double)instructions_executed/cycles_simulated);
  fprintf(stdout, "Ibuffer %dB capacity %dB blocksize\n", IBsize, 1<<IBblksz2);
  fprintf(stdout, "%12ld instruction buffer misses (%3.1f%%)\n",
	  ibuf_misses, 100.0*ibuf_misses/instructions_executed);

  fprintf(stdout, "Cache %ldB linesize %ldKB capacity %ld way\n", sc.line,
	  (sc.line*sc.rows*sc.ways)/1024, sc.ways);
  long reads = sc.refs-sc.updates;
  fprintf(stdout, "%12ld cache reads (%3.1f%%)\n", reads, 100.0*reads/instructions_executed);
  fprintf(stdout, "%12ld cache writes (%3.1f%%)\n", sc.updates, 100.0*sc.updates/instructions_executed);
  fprintf(stdout, "%12ld cache misses (%5.3f%%)\n", sc.misses, 100.0*sc.misses/instructions_executed);
  fprintf(stdout, "%12ld cache evictions (%5.3f%%)\n", sc.evictions,  100.0*sc.evictions/instructions_executed);

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
    perf.h->dc_misses = sc.misses;
    double kinsns = icount/1e3;
    fprintf(stderr, " IB=%3.0f SC$=%4.2f m/Ki", ibmisses/kinsns, sc.misses/kinsns);
  }
}

