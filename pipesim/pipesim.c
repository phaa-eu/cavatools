/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

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

#define DEFAULT_BPENALTY   2		/* cycles taken branch delay (pipeline bubble) */

#define DEFAULT_IPENALTY   5		/* cycles I-buffer refill penalty */
#define DEFAULT_ILGLINE    8		/* 2^ I-buffer line length in bytes */
#define DEFAULT_ILGBLKSZ   4		/* 2^ I-buffer block size in bytes */

#define DEFAULT_DPENALTY  25		/* cycles miss penalty */
#define DEFAULT_DLGLINE    6		/* 2^ cache line length in bytes */ 
#define DEFAULT_DLGSETS    6		/* 2^ cache lines per way */ 
#define DEFAULT_DWAYS      4		/* number of ways associativity */ 


#define REPORT_FREQUENCY 100
long report_frequency;
int quiet =0;
int timing =0;


struct ibuf_t* ib;
struct cache_t dcache;
struct fifo_t* trace_buffer;
struct fifo_t* l2;
int hart;
uint64_t mem_queue[tr_memq_len];

long branch_penalty;
long fetch_latency;
long lg_ib_line;

struct countSpace_t countSpace;


struct statistics_t stats;
long frame_header;


void countSpace_init(const char* shm_name, int reader)
{
  countSpace.base  = insnSpace.base;
  countSpace.bound = insnSpace.bound;
  size_t size = sizeof(struct count_t) * ((countSpace.bound - countSpace.base) / 2);
  if (!shm_name)
    countSpace.insn_array = (struct count_t*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  else if (!reader) {
    int fd = shm_open(shm_name, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
    dieif(fd<0, "shm_open() failed in countSpace_init");
    dieif(ftruncate(fd, size)<0, "ftruncate() failed in countSpace_init");
    countSpace.insn_array = (struct count_t*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  }
  else {
    int fd = shm_open(shm_name, O_RDONLY, 0);
    dieif(fd<0, "shm_open() failed in countSpace_init");
    countSpace.insn_array = (struct count_t*)mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
  }
  dieif(countSpace.insn_array==0, "mmap() failed in countSpace_init");
  if (!shm_name || !reader) {
    memset((char*)countSpace.insn_array, 0, size);
    for (Addr_t pc=countSpace.base; pc<countSpace.bound; pc+=2)
      decode_instruction(&countSpace.insn_array[(pc-countSpace.base)/2].i, pc);
  }
}



void status_report(struct statistics_t* stats)
{
  if (quiet)
    return;
  struct timeval *t1=&stats->start_timeval, t2;
  gettimeofday(&t2, 0);
  double msec = (t2.tv_sec - t1->tv_sec)*1000;
  msec += (t2.tv_usec - t1->tv_usec)/1000.0;
  fprintf(stderr, "\r%3.1fBi (%ld) %3.1fBc %3.1f Im/Ki %3.1f Dm/Ki %5.3f IPC %3.1f MIPS %3.1f CPS %3.1fs",
	  stats->insns/1e9, stats->segments, stats->cycles/1e9, ib->misses/(stats->insns/1e3), dcache.misses/(stats->insns/1e3),
	  (double)stats->insns/stats->cycles, stats->insns/(1e3*msec), stats->cycles/(1e3*msec), msec/1e3);
}


int main(int argc, const char** argv)
{
  assert(sizeof(struct insn_t) == 8);
  for (int i=0; i<Number_of_opcodes; i++)
    insnAttr[i].latency = fu_latency[insnAttr[i].unit];
  
  static const char* in_path =0;
  static const char* out_path =0;
  static const char* count_path =0;
  static const char* wflag =0;
  static const char* ilgline =0;
  static const char* ilgblksz =0;
  static const char* dlgline =0;
  static const char* dlgsets =0;
  static const char* dnumways =0;
  static const char* bpenalty =0;
  static const char* ipenalty =0;
  static const char* dpenalty =0;
  static const char* report =0;
  static struct options_t opt[] =
    {
     { "--in=",		.v=&in_path,	.h="Trace file from caveat =name" }, 
     { "--bdelay=",	.v=&bpenalty,	.h="Taken branch delay is =number cycles [2]" },
     { "--imiss=",	.v=&ipenalty,	.h="L0 instruction buffer refill latency is =number cycles [5]" },
     { "--iline=",	.v=&ilgline,	.h="L0 instruction buffer line size is 2^ =n bytes [8]" },
     { "--iblksz=",	.v=&ilgblksz,	.h="L0 instruction buffer block size is 2^ =n bytes [4]" },
     { "--dmiss=",	.v=&dpenalty,	.h="L1 data cache miss latency is =number cycles [25]" },
     { "--write=",	.v=&wflag,	.h="L1 data cache is write=[back|thru]" },
     { "--dline=",	.v=&dlgline,	.h="L1 data cache line size is 2^ =n bytes [6]" },
     { "--dways=",	.v=&dnumways,	.h="L1 data cache is =w ways set associativity [4]" },
     { "--dsets=",	.v=&dlgsets,	.h="L1 data cache has 2^ =n sets per way [6]" },
     { "--out=",	.v=&out_path,	.h="Create output trace file =name [no output trace]" },
     { "--count=",	.v=&count_path,	.h="Performance counters in shared memory =name [none]" },
     { "--timing",	.f=&timing,	.h="Include pipeline timing information in trace" },
     { "--report=",	.v=&report,	.h="Progress report every =number million instructions [100]" },
     { "--quiet",	.f=&quiet,	.h="Don't report progress to stderr" },
     { "-q",		.f=&quiet,	.h="short for --quiet" },
     { 0 }
    };
  int numopts = parse_options(opt, argv+1,
			      "pipesim --in=trace [pipesim-options] target-program");
  if (argc == numopts+1 || !in_path)
    help_exit();
  long entry = load_elf_binary(argv[1+numopts], 0);
  countSpace_init(count_path, 0);
  
  report_frequency = (report ? atoi(report) : REPORT_FREQUENCY) * 1000000;
  //  stats.start_tick = clock();
  gettimeofday(&stats.start_timeval, 0);
  trace_buffer = fifo_open(in_path);
  if (out_path)
    l2 = fifo_create(out_path, 0);
  /* branch related initialization */
  branch_penalty = bpenalty ? atoi(bpenalty) : DEFAULT_BPENALTY;
  /* initialize instruction buffer */
  ib = (struct ibuf_t*)malloc(sizeof(struct ibuf_t));
  memset((char*)ib, 0, sizeof(struct ibuf_t));
  ib->lg_line = ilgline ? atoi(ilgline) : DEFAULT_ILGLINE;
  ib->tag_mask = ~((1L << ib->lg_line)-1);
  ib->penalty = ipenalty ? atoi(ipenalty) : DEFAULT_IPENALTY;
  ib->lg_blksize = ilgblksz ? atoi(ilgblksz) : DEFAULT_ILGBLKSZ;
  ib->numblks = (1<<ib->lg_line)/(1<<ib->lg_blksize);
  ib->blk_mask = ib->numblks - 1;
  for (int i=0; i<2; i++) {
    ib->ready[i] = (long*)malloc(ib->numblks*sizeof(long));
    memset((char*)ib->ready[i], 0, ib->numblks*sizeof(long));
  }
  /* initialize cache */
  long read_latency = dpenalty ? atoi(dpenalty) : DEFAULT_DPENALTY;
  int lg_line_size    = dlgline ? atoi(dlgline) : DEFAULT_DLGLINE;
  int lg_rows_per_way = dlgsets ? atoi(dlgsets) : DEFAULT_DLGSETS;
  int dways = dnumways ? atoi(dnumways) : DEFAULT_DWAYS;
  struct lru_fsm_t* fsm;
  switch (dways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--dways=1..4 only\n");  exit(-1);
  }
  init_cache(&dcache, lg_line_size, lg_rows_per_way, fsm, !(wflag && wflag[0]=='t'));
  //  show_cache(&dcache);
  frame_header = tr_has_mem | (timing ? tr_has_timing : 0);
  if (wflag) {
    if (wflag[0] == 'b')
      slow_pipe(entry, read_latency, report_frequency, &dcache_writeback);
    else if (wflag[0] == 't')
      slow_pipe(entry, read_latency, report_frequency, &dcache_writethru);
    else
      help_exit();
  }
  else if (timing)
    slow_pipe(entry, read_latency, report_frequency, 0);
  else if (count_path)
    count_pipe(entry, read_latency, report_frequency, 0);
  else
    fast_pipe(entry, read_latency, report_frequency, 0);
  if (out_path) {
    fifo_put(l2, trM(tr_eof, 0));
    fifo_finish(l2);
  }
  fifo_close(trace_buffer);
  status_report(&stats);
  fprintf(stderr, "\n\n");
  fprintf(stderr, "%12ld instructions in %ld segments\n", stats.insns, stats.segments);
  fprintf(stderr, "%12ld cycles, %5.3f CPI\n", stats.cycles, (double)stats.insns/stats.cycles);
  fprintf(stderr, "Ibuffer %dB linesize %dB blocksize %dKbit capacity\n", 1<<ib->lg_line, 1<<ib->lg_blksize, 2*8*(1<<ib->lg_line)/1024);
  fprintf(stderr, "%12ld instruction buffer misses (%3.1f%%)\n", ib->misses, 100.0*ib->misses/stats.insns);
  fprintf(stderr, "Dcache %dB linesize %dKB capacity %d way\n", dcache.line,
	  (dcache.line*dcache.rows*dcache.ways)/1024, dcache.ways);
  long reads = dcache.refs-dcache.updates;
  fprintf(stderr, "%12ld L1 Dcache reads (%3.1f%%)\n", reads, 100.0*reads/stats.insns);
  fprintf(stderr, "%12ld L1 Dcache writes (%3.1f%%)\n", dcache.updates, 100.0*dcache.updates/stats.insns);
  fprintf(stderr, "%12ld L1 Dcache misses (%5.3f%%)\n", dcache.misses, 100.0*dcache.misses/stats.insns);
  fprintf(stderr, "%12ld L1 Dcache evictions (%5.3f%%)\n", dcache.evictions,  100.0*dcache.evictions/stats.insns);
  return 0;
}

