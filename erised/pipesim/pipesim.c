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
#include "cache.h"
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

#define DEFAULT_DPENALTY  25		/* cycles miss penalty */
#define DEFAULT_DLGLINE    6		/* 2^ cache line length in bytes */ 
#define DEFAULT_DLGSETS    6		/* 2^ cache lines per way */ 
#define DEFAULT_DWAYS      4		/* number of ways associativity */ 


#define REPORT_FREQUENCY 100000000
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
	  stats->insns/1e9, stats->segments, stats->cycles/1e9, dcache.misses/1e6, dcache.misses/(stats->insns/1e3),
	  (double)stats->insns/stats->cycles, stats->insns/(1e6*elapse_time));
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
  static const char* dways_option =0;
  static const char* dpenalty =0;
  static const char* report =0;
  static struct options_t flags[] =
    {  { "--in=",	.v = &in_path		},
       { "--out=",	.v = &out_path		},
       { "--write=",	.v = &wflag		},
       { "--dline=",	.v = &dlgline		},
       { "--dsets=",	.v = &dlgsets		},
       { "--dways=",	.v = &dways_option	},
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
  //  fprintf(stderr, "Text segment [0x%lx, 0x%lx)\n", insnSpace.base, insnSpace.bound);
  if (report)
    report_frequency = atoi(report);
  stats.start_tick = clock();
  trace_init(&trace_buffer, in_path, 1);
  if (out_path)
    fifo_init(&l2, out_path, 0);
  /* initialize cache */
  int lg_line_size    = dlgline ? atoi(dlgline) : DEFAULT_DLGLINE;
  int lg_rows_per_way = dlgsets ? atoi(dlgsets) : DEFAULT_DLGSETS;
  int dways = dways_option ? atoi(dways_option) : DEFAULT_DWAYS;
  struct lru_fsm_t* fsm;
  switch (dways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--ways=1..4 only\n");  exit(-1);
  }
  init_cache(&dcache, lg_line_size, lg_rows_per_way, fsm, !(wflag && wflag[0]=='t'));
  //  show_cache(&dcache);
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
  else if (visible)
    slow_pipe(entry, read_latency, report_frequency, 0);
  else
    fast_pipe(entry, read_latency, report_frequency, 0);
  if (out_path) {
    fifo_put(&l2, trM(tr_eof, 0));
    fifo_fini(&l2);
  }
  fifo_fini(&trace_buffer);
  status_report(&stats);
  fprintf(stderr, "\n\n");
  fprintf(stderr, "%12ld instructions in %ld segments\n", stats.insns, stats.segments);
  fprintf(stderr, "%12ld cycles, %5.3f CPI\n", stats.cycles, (double)stats.insns/stats.cycles);
  fprintf(stderr, "%12ld taken branches (%3.1f%%)\n", stats.branches_taken, 100.0*stats.branches_taken/stats.insns);
  fprintf(stderr, "Dcache %dB linesize %dKB capacity %d way\n", dcache.line,
	  (dcache.line*dcache.rows*dcache.ways)/1024, dcache.ways);
  long reads = dcache.refs-dcache.updates;
  fprintf(stderr, "%12ld L1 Dcache reads (%3.1f%%)\n", reads, 100.0*reads/stats.insns);
  fprintf(stderr, "%12ld L1 Dcache writes (%3.1f%%)\n", dcache.updates, 100.0*dcache.updates/stats.insns);
  fprintf(stderr, "%12ld L1 Dcache misses (%5.3f%%)\n", dcache.misses, 100.0*dcache.misses/stats.insns);
  fprintf(stderr, "%12ld L1 Dcache evictions (%5.3f%%)\n", dcache.evictions,  100.0*dcache.evictions/stats.insns);
  return 0;
}

