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


#define REPORT_FREQUENCY  1000000000


long report_frequency = REPORT_FREQUENCY;
time_t start_tick;
long insn_count =0;
long segments =0;
long pvr_cycles =0;
long pvr_cutoff =0;

struct fifo_t trace_buffer;
int hart;
uint64_t mem_queue[1024];

static inline void status_report()
{
  double elapse = (clock() - start_tick) / CLOCKS_PER_SEC;
  fprintf(stderr, "\r%3.1fB insns (%ld segments) in %3.1f seconds for %3.1f MIPS",
	  insn_count/1e9, segments, elapse, insn_count/1e6/elapse);
}


static struct fifo_t verify;

void do_nothing(long pc, int checking)
{
  long next_report =report_frequency;
  int ptr =0;			/* check pc circular buffer */
  for (uint64_t tr=fifo_get(&trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(&trace_buffer)) {
    /* must come first, but rare and branch prediction will skip */
    if (tr_code(tr) == tr_start) {
      hart = tr_number(tr);
      pc = tr_pc(tr);
      ++segments;
      continue;
    }
    if (is_mem(tr)) {
      continue;
    }
    long epc = pc + tr_number(tr);
    while (pc < epc) {
      if (checking) {
#define NUM  32
	static long circle[NUM];
	long vpc = fifo_get(&verify);
	while (vpc == 0L)	/* skip over breakpoints */
	  vpc = fifo_get(&verify);
	if (pc != vpc) {
	  char buf[1024];
	  for (int i=0; i<NUM; i++) {
	    long tpc = circle[(ptr+i)%NUM];
	    print_pc(tpc, stdout);
	    print_insn(tpc, stdout);
	  }
	  print_pc(pc, stdout);
	  print_insn(pc, stdout);
	  printf("PC MISMATCH, expecting\n");
	  print_pc(vpc, stdout);
	  print_insn(vpc, stdout);
	  exit(-1);
	}
	circle[ptr] = pc;
	ptr = (ptr+1) % NUM;
      }
      /* advance to next instruction */
      const struct insn_t* p = insn(pc);
      pc += shortOp(p->op_code) ? 2 : 4;
      if (++insn_count >= next_report) {
	status_report();
	next_report += report_frequency;
      }
    }
    if (is_goto(tr)) {
      pc = tr_pc(tr);
      if (tr_code(tr) == tr_start)
	hart = tr_number(tr);
    }
  }
}


void print_timing_trace(long begin, long end)
{
  if (pvr_cycles) {
    time_t T = time(NULL);
    struct tm tm = *localtime(&T);
    fprintf(stdout, "#Paraver (%02d/%02d/%02d at %02d:%02d):%ld:1(1):1:1(1:1)\n",
	    tm.tm_mday, tm.tm_mon+1, tm.tm_year, tm.tm_hour, tm.tm_min, pvr_cycles);
  }
  else {
    fprintf(stdout, "Timing trace [%lx, %lx]\n", begin, end);
  }
  long previous_time = 0;
  long cache_miss = 0;
  for (uint64_t tr=fifo_get(&trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(&trace_buffer)) {
    long pc, now;
    if (tr_code(tr) == tr_issue) {
      pc = tr_pc(tr);
      now = previous_time + tr_number(tr);
      if (begin <= pc && pc <= end) {
	if (pvr_cycles) {
	  if (now-previous_time > pvr_cutoff) {
	    fprintf(stdout, "2:0:1:1:1:%ld:%d:%ld\n", previous_time,  0, pc);
	    fprintf(stdout, "2:0:1:1:1:%ld:%d:%ld\n",           now, 10, pc);
	  }
	}
	else {
	  while (++previous_time < now)
	    fprintf(stdout, "%18s%8ld:\n", "", previous_time);
	  if (cache_miss) {
	    fprintf(stdout, "[%016lx]", cache_miss);
	    cache_miss = 0;
	  }
	  else
	    fprintf(stdout, "%18s", "");
	  fprintf(stdout, "%8ld: ", now);
	  
	  if (pvr_cycles)
	    fprintf(stdout, "2:0:1:1:1:%ld:%ld:%ld\n", now, now-previous_time, pc);
	  print_insn(tr_pc(tr), stdout);
	}
      }
      previous_time = now;
    }
    else if (tr_code(tr) == tr_d1get) {
      cache_miss = tr_value(tr);
    }
    if (pvr_cycles) {
      if (is_dcache(tr))
	fprintf(stdout, "2:0:1:1:1:%ld:%ld:%ld\n", now, tr_clevel(tr),    tr_value(tr));
      else if (is_icache(tr))
	fprintf(stdout, "2:0:1:1:1:%ld:%ld:%ld\n", now, tr_clevel(tr)+10, tr_value(tr));
      if (now >= pvr_cycles)
	return;
    }
  }
}


void print_listing(long pc)
{
  uint64_t* memq = mem_queue;	/* help compiler allocate in register */
  long tail =0;
  
  for (uint64_t tr=fifo_get(&trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(&trace_buffer)) {
    /* must come first, but rare and branch prediction will skip */
    if (tr_code(tr) == tr_start) {
      hart = tr_number(tr);
      pc = tr_pc(tr);
      continue;
    }
    if (is_mem(tr)) {
      memq[tail++] = tr_value(tr);
      continue;
    }
    static char buf[1024];
    long n;
    char bing = '@';
    
    long epc = pc + tr_number(tr);
    long head = 0;
    while (pc < epc) {
      print_pc(pc, stdout);
      const struct insn_t* p = insn(pc);
      if (is_mem(p->op_code))
	n = sprintf(buf, "%c[%016lx]", bing, memq[head++]);
      else if (is_goto(p->op_code))
	n = sprintf(buf, "%c<%016lx>", bing, tr_pc(tr));
      else
	n = sprintf(buf, "%c %16s ", bing, "");
      n = write(1, buf, n);
      print_insn(pc, stdout);
      ++insn_count;
      pc += shortOp(p->op_code) ? 2 : 4;
      bing = ' ';
    }
    if (is_goto(tr))
      pc = tr_pc(tr);
    tail = 0;
  }
}


long atohex(const char* p)
{
  for (long n=0; ; p++) {
    long digit;
    if ('0' <= *p && *p <= '9')
      digit = *p - '0';
    else if ('a' <= *p && *p <= 'f')
      digit = 10 + (*p - 'a');
    else if ('A' <= *p && *p <= 'F')
      digit = 10 + (*p - 'F');
    else
      return n;
    n = 16*n + digit;
  }
}


int main(int argc, const char** argv)
{
  static const char* shm_path =0;
  static int list =0;
  static int trace =0;
  static const char* see_range =0;
  
  static const char* veri_path =0;
  static const char* report =0;
  static const char* paraver =0;
  static const char* cutoff =0;
  
  static struct options_t flags[] =
    {  { "--in=",	.v = &shm_path		},
       { "--list",	.f = &list		},
       { "--trace",	.f = &trace		},
       { "--see=",	.v = &see_range		},
       { "--report=",	.v = &report		},
       { "--verify=",	.v = &veri_path		},
       { "--paraver=",	.v = &paraver		},
       { "--cutoff=",	.v = &cutoff		},
       { 0					}
    };
  int numopts = parse_options(flags, argv+1);
  if (!shm_path) {
    fprintf(stderr, "usage: traceinfo --in=shm_path <other options> elf_binary\n");
    exit(0);
  }
  if (report)
    report_frequency = atoi(report);
  long entry = load_elf_binary(argv[1+numopts], 0);
  trace_init(&trace_buffer, shm_path, 1);
  start_tick = clock();
  if (list)
    print_listing(entry);
  else if (trace)
    print_timing_trace(insnSpace.base, insnSpace.bound);
  else if (paraver || see_range) {
    if (paraver) {
      pvr_cycles = atoi(paraver);
      if (cutoff)
	pvr_cutoff = atoi(cutoff);
    }
    long begin = insnSpace.base;
    long end = insnSpace.bound;
    if (see_range) {
      begin = atohex(see_range);
      const char* comma = strchr(see_range, ',');
      end = atohex(comma+1);
    }
    print_timing_trace(begin, end);
  }
  else {
    if (veri_path)
      fifo_init(&verify, veri_path, 1);
    do_nothing(entry, veri_path!=0);
    if (veri_path)
      fifo_fini(&verify);
  }
  trace_fini(&trace_buffer);
  fprintf(stderr, "\n%ld Instructions in trace\n", insn_count);
  return 0;
}
