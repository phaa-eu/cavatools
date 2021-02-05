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

unsigned char fu_latency[Number_of_units] =
  { [Unit_a] = 4,	/* FP Adder */
    [Unit_b] = 1,	/* Branch unit */
    [Unit_f] = 4,	/* FP fused Multiply-Add */
    [Unit_i] = 1,	/* Scalar Integer ALU */
    [Unit_j] = 1,	/* Media Integer ALU */
    [Unit_m] = 4,	/* FP Multipler*/
    [Unit_n] = 8,	/* Scalar Integer Multipler */
    [Unit_r] = 2,	/* Load unit */
    [Unit_s] = 1,	/* Scalar Shift unit */
    [Unit_t] = 1,	/* Media Shift unit */
    [Unit_w] = 1,	/* Store unit */
    [Unit_x] = 5,	/* Special unit */
  };

static const char *in_path, *out_path, *perf_path, *wflag;

const struct options_t opt[] =
  {
   { "--in=s",		.s=&in_path,		.ds=0,		.h="Trace file from caveat =name" },
   { "--perf=s",	.s=&perf_path,		.ds=0,		.h="Performance counters in shared memory =name" },
     
   { "--bdelay=i",	.i=&ib.delay,		.di=2,		.h="Taken branch delay is =number cycles" },
   { "--bmiss=i",	.i=&ib.penalty,		.di=5,		.h="L0 instruction buffer refill latency is =number cycles" },
   { "--bufsz=i",	.i=&ib.bufsz,		.di=7,		.h="L0 instruction buffer capacity is 2*2^ =n bytes" },
   { "--blocksz=i",	.i=&ib.blksize,		.di=4,		.h="L0 instruction buffer block size is 2^ =n bytes" },
     
   { "--imiss=i",	.i=&ic.penalty,		.di=25,		.h="L1 instruction cache miss latency is =number cycles" },
   { "--iline=i",	.i=&ic.lg_line,		.di=6,		.h="L1 instrucdtion cache line size is 2^ =n bytes" },
   { "--iways=i",	.i=&ic.ways,		.di=4,		.h="L1 instrucdtion cache is =n ways set associativity" },
   { "--isets=i",	.i=&ic.lg_rows,		.di=6,		.h="L1 instrucdtion cache has 2^ =n sets per way" },
     
   { "--dmiss=i",	.i=&dc.penalty,		.di=25,		.h="L1 data cache miss latency is =number cycles" },
   { "--write=s",	.s=&wflag,		.ds="b",	.h="L1 data cache is write=[back|thru]" },
   { "--dline=i",	.i=&dc.lg_line,		.di=6,		.h="L1 data cache line size is 2^ =n bytes" },
   { "--dways=i",	.i=&dc.ways,		.di=4,		.h="L1 data cache is =w ways set associativity" },
   { "--dsets=i",	.i=&dc.lg_rows,		.di=6,		.h="L1 data cache has 2^ =n sets per way" },
     
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

struct ibuf_t ib;
struct cache_t ic, dc;
struct fifo_t* in;
struct fifo_t* out;
int hart;
uint64_t mem_queue[tr_memq_len];




int main(int argc, const char** argv)
{
  assert(sizeof(struct insn_t) == 8);
  gettimeofday(&start_time, 0);
  for (int i=0; i<Number_of_opcodes; i++)
    insnAttr[i].latency = fu_latency[insnAttr[i].unit];

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
  
  /* initialize instruction buffer */
  ib.tag_mask = ~( (1L << (ib.bufsz-1)) - 1 );
  ib.numblks = (1<<ib.bufsz)/(1<<ib.blksize) - 1;
  ib.blk_mask = ib.numblks - 1;
  for (int i=0; i<2; i++) {
    ib.ready[i] = (long*)malloc(ib.numblks*sizeof(long));
    memset((char*)ib.ready[i], 0, ib.numblks*sizeof(long));
  }

  /* initialize instruction cache */
  struct lru_fsm_t* fsm;
  switch (ic.ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--iways=1..4 only\n");  exit(-1);
  }
  init_cache(&ic, fsm, 0);
  
  /* initialize data cache */
  switch (dc.ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--dways=1..4 only\n");  exit(-1);
  }
  init_cache(&dc,fsm, !(wflag && wflag[0]=='t'));

  long (*model_dcache)(long tr, const struct insn_t* p, long available) = &dcache_writeback;;
  if (wflag) {
    if (strcmp(wflag, "thru") == 0)
      model_dcache = &dcache_writethru;
    else if (strcmp(wflag, "back") == 0)
      help_exit();
  }
  if (out_path) {
    if (perf_path)
      trace_count_pipe(report, model_dcache);
    else
      trace_pipe(report, model_dcache);
    fifo_put(out, trM(tr_eof, 0));
    fifo_finish(out);
  }
  else {
    if (perf_path)
      count_pipe(report, model_dcache);
    else
      fast_pipe(report, 0);
  }
  if (perf_path)
    perf_close(&perf);
  fifo_close(in);
  
  fprintf(stderr, "\n\n");
  fprintf(stdout, "%12ld instructions executed\n", instructions_executed);
  fprintf(stdout, "%12ld cycles simulated\n", cycles_simulated);
  fprintf(stdout, "%12.3f IPC\n", (double)instructions_executed/cycles_simulated);
  fprintf(stdout, "Ibuffer %ldB capacity %ldB blocksize\n", 1L<<ib.bufsz, 1L<<ib.blksize);
  fprintf(stdout, "%12ld instruction buffer misses (%3.1f%%)\n",
	  ib.misses, 100.0*ib.misses/instructions_executed);
  
  fprintf(stdout, "Icache %ldB linesize %ldKB capacity %ld way\n", ic.line,
	  (ic.line*ic.rows*ic.ways)/1024, ic.ways);
  long reads = ic.refs-ic.updates;
  fprintf(stdout, "%12ld L1 Icache reads (%3.1f%%)\n", reads, 100.0*reads/instructions_executed);

  fprintf(stdout, "Dcache %ldB linesize %ldKB capacity %ld way\n", dc.line,
	  (dc.line*dc.rows*dc.ways)/1024, dc.ways);
  reads = dc.refs-dc.updates;
  fprintf(stdout, "%12ld L1 Dcache reads (%3.1f%%)\n", reads, 100.0*reads/instructions_executed);
  fprintf(stdout, "%12ld L1 Dcache writes (%3.1f%%)\n", dc.updates, 100.0*dc.updates/instructions_executed);
  fprintf(stdout, "%12ld L1 Dcache misses (%5.3f%%)\n", dc.misses, 100.0*dc.misses/instructions_executed);
  fprintf(stdout, "%12ld L1 Dcache evictions (%5.3f%%)\n", dc.evictions,  100.0*dc.evictions/instructions_executed);

  return 0;
}



void status_report(long now, long icount)
{
  instructions_executed = icount;
  cycles_simulated = now;
  if (quiet)
    return;
  struct timeval this_time;
  gettimeofday(&this_time, 0);
  double msec = (this_time.tv_sec - start_time.tv_sec)*1000;
  msec += (this_time.tv_usec - start_time.tv_usec)/1000.0;
  fprintf(stderr, "\r%3.1fBi %3.1fBc IPC=%5.3f CPS=%5.3f in %lds",
	  icount/1e9, now/1e9, (double)icount/now, now/(1e3*msec), (long)(msec/1e3));
  if (perf_path) {
    perf.h->insns = icount;
    perf.h->cycles = now;
    perf.h->ib_misses = ib.misses;
    perf.h->ic_misses = ic.misses;
    perf.h->dc_misses = dc.misses;
    double kinsns = icount/1e3;
    fprintf(stderr, " IB=%3.0f I$=%5.3f D$=%4.2f m/Ki",
	    ib.misses/kinsns, ic.misses/kinsns, dc.misses/kinsns);
  }
}


long dcache_writethru(long tr, const struct insn_t* p, long available)
{
  long addr = tr_value(tr);
  long tag = addr >> dc.lg_line;
  long when = 0;
  if (writeOp(p->op_code)) {
    long sz = tr_size(tr);
    if (sz < 8) {	/* < 8B need L1 for ECC, 8B do not allocate */
      when = lookup_cache(&dc, addr, 0, available);
      if (when == available)
	fifo_put(out, trM(tr_d1get, addr));
    }
    fifo_put(out, tr);
  }
  else
    when = lookup_cache(&dc, addr, 0, available);
  if (when == available) { /* cache miss */
    fifo_put(out, trM(tr_d1get, addr));
  }
  return when;
}


long dcache_writeback(long tr, const struct insn_t* p, long available)
{
  long addr = tr_value(tr);
  long tag = addr >> dc.lg_line;
  long when = lookup_cache(&dc, addr, writeOp(p->op_code), available);
  if (when == available) { /* cache miss */
    if (*dc.evicted)
      fifo_put(out, trM(tr_d1put, *dc.evicted<<dc.lg_line));
    fifo_put(out, trM(tr_d1get, addr));
  }
  return when;
}
