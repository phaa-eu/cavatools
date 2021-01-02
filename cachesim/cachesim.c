/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"

#include "types.h"
#include "tagonlycache.h"


#define REPORT_FREQUENCY  10

#define RT_READ		0b0000000000000010L
#define RT_WRITE	0b0000000000000100L
#define RT_GETPUT	0b0000000000010000L
#define RT_LEVEL_SHIFT	5
#define RT_L0_CACHE	0b0000000000100000L
#define RT_L1_CACHE	0b0000000001000000L
#define RT_L2_CACHE	0b0000000010000000L
#define RT_L3_CACHE	0b0000000100000000L
#define RT_INSN_CACHE	0b0000001000000000L
#define RT_DATA_CACHE	0b0000010000000000L



struct fifo_t* fifo;
struct fifo_t* outbuf;
long report_frequency;

void allocError(uint64 n, char * thing, char * filename, int32 linenumber)
{
  printf("\nmemory allocation failed trying to allocate %lld bytes for a '%s' on line %d in file %s",
	 n, thing, linenumber, filename);
}

int strchrs(const char* str, const char* keys)
{
  for (char k=*keys++; k; k=*keys++)
    if (strchr(str, k))
      return 1;
  return 0;
}

static const char* in_path;
static const char* out_path;
static const char* flags;
static long lgline, ways, lgsets;
static long report, quiet;

const struct options_t opt[] =
  {  { "--in=s",	.s=&in_path,	.ds=0,	.h="Trace file =name (from caveat, pipesim, or cachesim)" },
     { "--line=i",	.i=&lgline,	.di=6,	.h="Cache line size is 2^ =n bytes" },
     { "--ways=i",	.i=&ways,	.di=8,	.h="Cache is =w ways set associativity" },
     { "--sets=i",	.i=&lgsets,	.di=11,	.h="Cache has 2^ =n sets per way" },
     { "--sim=s",	.s=&flags,	.ds=0,	.h="Simulate all access =types [iIdD0123rRwW] default all" },
     { "--out=s",	.s=&out_path,	.ds=0,	.h="Output next-level misses to trace =name" },
     { "--report=i",	.i=&report,	.di=10,	.h="Progress report every =number million instructions" },
     { "--quiet",	.b=&quiet,	.bv=1,	.h="Don't report progress to stderr" },
     { "-q",		.b=&quiet,	.bv=1,	.h="short for --quiet" },
     { 0 }
  };
const char* usage = "cachesim --in=trace [cachesim-options]";

int main(int argc, const char** argv)
{
  int numopts = parse_options(argv+1);
  if (argc == numopts+1 || !in_path)
    help_exit();
  cacheData* cache = (cacheData*)newCacheData();
  configureCache(cache, (char*)"Wilson's cache", ways, lgline, lgsets);
  long insns=0, now=0, refs=0, misses=0;
  report_frequency = (report ? report : REPORT_FREQUENCY) * 1000000;
  long next_report = report_frequency;
  long filter = 0L;
  if (flags) {
    if (strchrs(flags, "rR") && !strchrs(flags, "wW"))
      filter |= RT_READ;
    else if (strchrs(flags, "wW") && !strchrs(flags, "rR"))
      filter |= RT_WRITE;
    else
      filter |= RT_READ | RT_WRITE;

    if (strchrs(flags, "iI") && !strchrs(flags, "dD"))
      filter |= RT_INSN_CACHE;
    else if (strchrs(flags, "dD") && !strchrs(flags, "iI"))
      filter |= RT_DATA_CACHE;
    else
      filter |= RT_INSN_CACHE | RT_DATA_CACHE;

    filter |= RT_L0_CACHE;
    if (!strchrs(flags, "0")) {
      filter |= RT_L1_CACHE;
      if (!strchrs(flags, "1")) {
	filter |= RT_L2_CACHE;
	if (!strchrs(flags, "2"))
	  filter |= RT_L3_CACHE;
      }
    }
  }
  else
    filter = ~0L;		/* default simulate all references */
    
  fifo = fifo_open(in_path);
  if (out_path)
    outbuf = fifo_create(out_path, 0);
  clock_t start_tick = clock();
  
  for (uint64_t tr=fifo_get(fifo); tr_code(tr)!=tr_eof; tr=fifo_get(fifo)) {
    
    if (is_mem(tr)) {
      long reftype;
      if (is_ldst(tr))
	reftype = is_write(tr) ? RT_WRITE : RT_READ;
      else {
	reftype = RT_GETPUT;
	reftype |= (1L<<tr_clevel(tr)) << RT_LEVEL_SHIFT;
      }
      if (reftype & filter) {
	cacheWay* way;
	++refs;
	if (!lookup(is_write(tr)?'w':'r', tr_value(tr), cache, &way)) {
	  ++misses;
	  if (out_path)
	    fifo_put(outbuf, tr);
	}
	if (refs >= next_report && !quiet) {
	  double elapse = (clock() - start_tick) / CLOCKS_PER_SEC;
	  fprintf(stderr, "\r%3.1fB insns %3.1fB cycles %3.1fB refs %3.1f misses/Kinsns in %3.1fs for %3.1f MIPS    ", insns/1e9, now/1e9, refs/1e9, misses/(insns/1e3), elapse, insns/1e6/elapse);
	  next_report += REPORT_FREQUENCY;
	}
      }
      else if (out_path)		/* pass to next stage */
	fifo_put(outbuf, tr);
      continue;
    }
    if (tr_code(tr) == tr_cycles) {
      now = tr_value(tr);
      if (out_path)
	fifo_put(outbuf, tr);
      continue;
    }
    if (tr_code(tr) == tr_icount) {
      insns = tr_value(tr);
      if (out_path)
	fifo_put(outbuf, tr);
      continue;
    }
    if (is_frame(tr) && out_path) {
      fifo_put(outbuf, tr);
      continue;
    }
  }
  fprintf(stderr, "\n\n");
  reportCacheStats(cache);
  printf("\n");
  if (out_path) {
    fifo_put(outbuf, trM(tr_eof, 0));
    fifo_finish(outbuf);
  }
  fifo_close(fifo);
  return 0;
}
