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
long mem_refs =0;
long segments =0;
long pvr_cycles =0;
long pvr_cutoff =0;

struct fifo_t* trace_buffer;
int hart;
uint64_t mem_queue[tr_memq_len];


static inline void status_report()
{
  double elapse = (clock() - start_tick) / CLOCKS_PER_SEC;
  fprintf(stderr, "\r%3.1fB insns %3.1fB mems (%ld segments) in %3.1f seconds for %3.1f MIPS",
	  insn_count/1e9, mem_refs/1e9, segments, elapse, insn_count/1e6/elapse);
}


void stat_mem_trace(long pc)
{
  long next_report =report_frequency;
  int withregs =0;
  for (uint64_t tr=fifo_get(trace_buffer); tr!=tr_eof; tr=fifo_get(trace_buffer)) {
    if (is_mem(tr)) {
      mem_refs++;
      continue;
    }
    if (is_bbk(tr))
      continue;
    if (is_frame(tr)) {
      hart = tr_delta(tr);
      pc = tr_pc(tr);
      fprintf(stderr, "Frame(pc=%d, mem=%d, reg=%d), hart#=%d, pc=0x%lx\n", (tr&tr_has_pc)!=0, (tr&tr_has_mem)!=0, (tr&tr_has_reg)!=0, hart, pc);
      dieif(tr & tr_has_reg, "trace with register updates must be viewed with program binary");
      ++segments;
      continue;
    }
    if (tr_code(tr) == tr_icount) {
      insn_count = tr_value(tr);
      if (insn_count >= next_report) {
	status_report();
	next_report += report_frequency;
      }
      continue;
    }
    /* ignore other trace record types */
  }
}


void stat_pc_trace(long pc)
{
  long next_report =report_frequency;
  int withregs =0;
  for (uint64_t tr=fifo_get(trace_buffer); tr!=tr_eof; tr=fifo_get(trace_buffer)) {
    if (is_mem(tr)) {
      mem_refs++;
      continue;
    }
    if (is_bbk(tr)) {
      if (withregs) {
	long epc = pc + tr_delta(tr);
	while (pc < epc) {
	  const struct insn_t* p = insn(pc);
	  if (p->op_rd != NOREG) {
	    long val = fifo_get(trace_buffer);
	  }
	  pc += shortOp(p->op_code) ? 2 : 4;
	}
      }
      else
	pc += tr_delta(tr);
      if (is_goto(tr))
	pc = tr_pc(tr);
      continue;
    }
    if (is_frame(tr)) {
      hart = tr_delta(tr);
      pc = tr_pc(tr);
      withregs = (tr & tr_has_reg) != 0;
      fprintf(stderr, "Frame(pc=%d, mem=%d, reg=%d), hart#=%d, pc=0x%lx\n", (tr&tr_has_pc)!=0, (tr&tr_has_mem)!=0, withregs, hart, pc);
      ++segments;
      continue;
    }
    if (tr_code(tr) == tr_icount) {
      insn_count = tr_value(tr);
      if (insn_count >= next_report) {
	status_report();
	next_report += report_frequency;
      }
      continue;
    }
    /* ignore other trace record types */
  }
}

void print_paraver_trace(long begin, long end)
{
  time_t T = time(NULL);
  struct tm tm = *localtime(&T);
  fprintf(stdout, "#Paraver (%02d/%02d/%02d at %02d:%02d):%ld:1(1):1:1(1:1)\n",
	  tm.tm_mday, tm.tm_mon+1, tm.tm_year, tm.tm_hour, tm.tm_min, pvr_cycles);
  fprintf(stdout, "Paraver trace [%lx, %lx]\n", begin, end);
  long now =0;
  for (uint64_t tr=fifo_get(trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(trace_buffer)) {
    if (tr_code(tr) == tr_stall) {
      long stall_begin = tr_number(tr);
      tr = fifo_get(trace_buffer);
      now = stall_begin + tr_delta(tr);
      if (now >= pvr_cycles)
	break;
      if (tr_delta(tr) >= pvr_cutoff) {
	long pc = tr_pc(tr);
	fprintf(stdout, "2:0:1:1:1:%ld:0:%ld\n",  stall_begin, pc);
	fprintf(stdout, "2:0:1:1:1:%ld:10:%ld\n", now,         pc);
      }
      continue;
    }
    if (is_mem(tr)) {
      if (is_dcache(tr))
	fprintf(stdout, "2:0:1:1:1:%ld:%ld:%ld\n", now, tr_clevel(tr),    tr_value(tr));
      else if (is_icache(tr))
	fprintf(stdout, "2:0:1:1:1:%ld:%ld:%ld\n", now, tr_clevel(tr)+10, tr_value(tr));
      /* we ignore individual loads and stores */
      continue;
    }
    if (is_frame(tr)) {
      hart = tr_delta(tr);
      int withtime = (tr & tr_has_timing) != 0;
      fprintf(stderr, "Frame(pc=%d, mem=%d, reg=%d, timing=%d), hart#=%d, pc=0x%lx\n", (tr&tr_has_pc)!=0, (tr&tr_has_mem)!=0,  (tr&tr_has_reg)!=0, withtime, hart, tr_pc(tr));
      if (!withtime) {
	fprintf(stderr, "Cannot make paraver trace from trace without timing!\n");
	exit(-1);
      }
      continue;
    }
    if (tr_code(tr) == tr_icount) {
      insn_count = tr_value(tr);
      continue;
    }
    tr_print(tr, stderr);
  }
}


void print_listing(long pc)
{
  uint64_t* memq = mem_queue;	/* help compiler allocate in register */
  long tail =0;
  int withregs =0;
  for (uint64_t tr=fifo_get(trace_buffer); tr!=tr_eof; tr=fifo_get(trace_buffer)) {
    if (is_mem(tr)) {
      memq[tail++] = tr_value(tr);
      continue;
    }
    if (is_bbk(tr)) {
      static char buf[1024];
      char bing = is_goto(tr) ? '@' : '!';
      long epc = pc + tr_delta(tr);
      long head = 0;
      while (pc < epc) {
	const struct insn_t* p = insn(pc);
	if (memOp(p->op_code))
	  printf("%c[%016lx]", bing, memq[head++]);
	else if (insnAttr[p->op_code].unit == Unit_b && (pc+(shortOp(p->op_code)?2:4)) == epc)
	  printf("%c<%16lx>", bing, tr_pc(tr));
	else
	  printf("%c %16s ", bing, "");
	if (withregs) {
	  if (p->op_rd == NOREG && p->op_rd != 0)
	    printf("%22s", "");
	  else {
	    long val = fifo_get(trace_buffer);
	    printf("%4s=%016lx ", regName[p->op_rd], val);
	  }
	}
	print_pc(pc, stdout);
	print_insn(pc, stdout);
	++insn_count;
	pc += shortOp(p->op_code) ? 2 : 4;
	bing = ' ';
      }
      if (is_goto(tr))
	pc = tr_pc(tr);
      tail = 0;
      continue;
    }
    if (is_frame(tr)) {
      hart = tr_delta(tr);
      pc = tr_pc(tr);
      withregs = (tr & tr_has_reg) != 0;
      int withpc = (tr & tr_has_pc) != 0;
      fprintf(stderr, "Frame(pc=%d, mem=%d, reg=%d, timing=%d), hart#=%d, pc=0x%lx\n", withpc, (tr&tr_has_mem)!=0, withregs, (tr&tr_has_timing)!=0, hart, pc);
      if (!withpc) {
	fprintf(stderr, "Cannot print listing of trace without pc!\n");
	exit(-1);
      }
      continue;
    }
    /* ignore other trace record types */
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
  
  static const char* report =0;
  static const char* paraver =0;
  static const char* cutoff =0;
  
  static struct options_t opt[] =
    {  { "--in=",	.v=&shm_path,	.h="Trace file from any cavatools =name" },
       { "--list",	.f=&list,	.h="Print assembly listing (only traces from caveat)" },
       { "--range=",	.v=&see_range,	.h="Only interested in =begin,end addresses (Hex no 0x) [all]" },
       { "--paraver=",	.v=&paraver,	.h="Make Paraver trace of =cycles to stdout" },
       { "--cutoff=",	.v=&cutoff,	.h="Ignore pipeline stalls less than =number cycles [1]" },
       { "--report=",	.v=&report,	.h="Progress report every =number million instructions [1000]" },
       { 0 }
    };
  int numopts = parse_options(opt, argv+1,
			      "traceinfo --in=trace [traceinfo-options] target-program"
			      "\n\t- summarize trace, make listing or create derivative traces");
  if (argc < 2)
    help_exit();
  report_frequency = (report ? atoi(report) : REPORT_FREQUENCY) * 1000000;
  long entry =0;
  if (argc > numopts+1)
    entry = load_elf_binary(argv[1+numopts], 0);
  trace_buffer = fifo_open(shm_path);
  start_tick = clock();
  if (list)
    print_listing(entry);
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
    print_paraver_trace(begin, end);
  }
  else if (argc > numopts+1)
    stat_pc_trace(entry);
  else
    stat_mem_trace(entry);
  fifo_close(trace_buffer);
  fprintf(stderr, "\n%ld Instructions, %ld memory references in trace\n", insn_count, mem_refs);
  return 0;
}
