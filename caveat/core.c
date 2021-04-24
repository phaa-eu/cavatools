/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "caveat.h"
#include "opcodes.h"
#include "caveat_fp.h"
#include "arith.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"
#include "cache.h"
#include "perfctr.h"

struct fifo_t* trace;
unsigned long lrsc_set = 0;	/* global atomic lock */


void init_core( struct core_t* cpu, long start_tick, const struct timeval* start_timeval )
{
  memset(cpu, 0, sizeof(struct core_t));
  for (int i=32; i<64; i++)	/* initialize FP registers to boxed float 0 */
    cpu->reg[i].ul = 0xffffffff00000000UL;
  cpu->counter.start_tick = start_tick;
  cpu->counter.start_timeval = *start_timeval;
}

void status_report( struct core_t* cpu, FILE* f )
{
  if (cpu->params.quiet)
    return;
  struct timeval *t1=&cpu->counter.start_timeval, t2;
  gettimeofday(&t2, 0);
  double msec = (t2.tv_sec - t1->tv_sec)*1000;
  msec += (t2.tv_usec - t1->tv_usec)/1000.0;
  double mips = cpu->counter.insn_executed / (1e3*msec);
  double icount = cpu->counter.insn_executed;
  double now = cpu->counter.cycles_simulated;
  fprintf(stderr, "\r%3.1fBi %3.1fBc IPC=%5.3f in %lds at %3.1f MIPS",
	  icount/1e9, now/1e9, (double)icount/now, (long)(msec/1e3), mips);
  if (cpu->params.simulate)
    fprintf(stderr, " I$=%5.3f/Mi D$=%4.2f/Ki", icache.misses/(icount/1e6), dcache.misses/(icount/1e3));
  if (perf.h) {
    perf.h->insns = icount;
    perf.h->cycles = now;
    perf.h->ic_misses = icache.misses;
    perf.h->dc_misses = dcache.misses;
  }
}
