/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "pipesim.h"

unsigned char fu_latency[Number_of_units] =
  { 4,	/* a FP Adder */
    1,	/* b Branch unit */
    4,	/* f FP fused Multiply-Add */
    1,	/* i Scalar Integer ALU */
    1,	/* j Media Integer ALU */
    4,	/* m FP Multipler*/
    8,	/* n Scalar Integer Multipler */
    2,	/* r Load unit */
    1,	/* s Scalar Shift unit */
    1,	/* t Media Shift unit */
    1,	/* w Store unit */
    5,	/* x Special unit */
  };

#define DEFAULT_DPENALTY  25	/* cycles miss penalty */
#define DEFAULT_DLGLINE    6	/* 2^ cache line length in bytes */ 
#define DEFAULT_DLGSETS    6	/* 2^ cache lines per way */ 


#define REPORT_FREQUENCY 1000000000
long report_frequency = REPORT_FREQUENCY;
int quiet =0;
int visible =0;


struct cache_t dcache;
struct fifo_t trace_buffer;
struct fifo_t l2;
int hart;
uint64_t mem_queue[tr_memq_len];


struct statistics_t stats;


void status_report(struct statistics_t* stats)
{
  if (quiet)
    return;
  double elapse_time = (clock()-stats->start_tick) / CLOCKS_PER_SEC;
  fprintf(stderr, "\r%3.1fB insns (%ld segments) %3.1fB cycles %3.1fM Dmisses %3.1f Dmiss/Kinsns %5.3f IPC %3.1f MIPS",
	  stats->instructions/1e9, stats->segments, stats->cycles/1e9, dcache.misses/1e6, dcache.misses/(stats->instructions/1e3),
	  (double)stats->instructions/stats->cycles, stats->instructions/(1e6*elapse_time));
}


int main(int argc, const char** argv)
{
  assert(sizeof(struct insn_t) == 8);
  for (int i=0; i<Number_of_opcodes; i++)
    insnAttr[i].latency = fu_latency[insnAttr[i].unit];
  
  static const char* in_path =0;
  static const char* out_path =0;
  static const char* wflag =0;
  static const char* dlgline =0;
  static const char* dlgsets =0;
  static const char* dpenalty =0;
  static const char* report =0;
  static struct options_t flags[] =
    {  { "--in=",	.v = &in_path		},
       { "--out=",	.v = &out_path		},
       { "--write=",	.v = &wflag		},
       { "--dline=",	.v = &dlgline		},
       { "--dsets=",	.v = &dlgsets		},
       { "--dpenalty=",	.v = &dpenalty		},
       { "--report=",	.v = &report		},
       { "--quiet",	.f = &quiet		},
       { "--visible",	.f = &visible		},
       { 0					}
    };
  int numopts = parse_options(flags, argv+1);
  if (!in_path) {
    fprintf(stderr, "usage: pipesim --in=in_shm [--w=back|thru] [--out=out_shm] elf_binary\n");
    exit(0);
  }
  long entry = load_elf_binary(argv[1+numopts], 0);
  fprintf(stderr, "Text segment [0x%lx, 0x%lx)\n", insnSpace.base, insnSpace.bound);
  if (report)
    report_frequency = atoi(report);
  stats.start_tick = clock();
  trace_init(&trace_buffer, in_path, 1);
  if (out_path)
    fifo_init(&l2, out_path, 0);
  {				/* initialize cache */
    int lg_line_size    = dlgline ? atoi(dlgline) : DEFAULT_DLGLINE;
    int lg_rows_per_way = dlgsets ? atoi(dlgsets) : DEFAULT_DLGSETS;
    if (wflag && wflag[0] == 't') /* dcache not writeable */
      init_cache(&dcache, lg_line_size, lg_rows_per_way, D_WAYS, 0);
    else
      init_cache(&dcache, lg_line_size, lg_rows_per_way, D_WAYS, 1);
  }
  long read_latency = dpenalty ? atoi(dpenalty) : DEFAULT_DPENALTY;
  if (wflag) {
    if (wflag[0] == 'b')
      slow_pipe(entry, read_latency, report_frequency, &dcache_writeback);
    else if (wflag[0] == 't')
      slow_pipe(entry, read_latency, report_frequency, &dcache_writethru);
    else {
      fprintf(stderr, "usage: pipesim --in=shm_path [--write=back|thru] ... elf_binary\n");
      exit(0);
    }
  }
  else
    fast_pipe(entry, read_latency, report_frequency, 0);
  if (out_path) {
    fifo_put(&l2, trM(tr_eof, 0));
    fifo_fini(&l2);
  }
  fifo_fini(&trace_buffer);
  status_report(&stats);
  fprintf(stderr, "\n\n");
  fprintf(stderr, "%12ld instructions executed\n", stats.instructions);
  fprintf(stderr, "%12ld taken branches (%4.1f%%)\n", stats.branches_taken, 100.0*stats.branches_taken/stats.instructions);
  fprintf(stderr, "%12ld memory reads (%4.1f%%)\n", stats.mem_refs-stats.stores, 100.0*(stats.mem_refs-stats.stores)/stats.instructions);
  fprintf(stderr, "%12ld memory writes (%4.1f%%)\n", stats.stores, 100.0*stats.stores/stats.instructions);
  fprintf(stderr, "%12ld L1 Dcache reads\n", dcache.refs-dcache.updates);
  fprintf(stderr, "%12ld L1 Dcache writes\n", dcache.updates);
  fprintf(stderr, "%12ld L1 Dcache misses (%6.3f%%)\n", dcache.misses, 100.0*dcache.misses/stats.instructions);
  fprintf(stderr, "%12ld L1 Dcache evictions (%6.3f%%)\n", dcache.evictions,  100.0*dcache.evictions/stats.instructions);
  return 0;
}

