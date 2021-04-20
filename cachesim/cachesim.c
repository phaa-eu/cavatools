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

void allocError(uint64 n, char * thing, char * filename, int32 linenumber)
{
  printf("\nmemory allocation failed trying to allocate %lld bytes for a '%s' on line %d in file %s",
	 n, thing, linenumber, filename);
}

static const char* in_path;
static long lgline, ways, lgsets;
static long report, quiet;
struct fifo_t* fifo;
long report_frequency;
clock_t start_tick;
long refs, misses;

const struct options_t opt[] =
  {  { "--in=s",	.s=&in_path,	.ds=0,	.h="Trace file =name (from caveat, pipesim, or cachesim)" },
     { "--line=i",	.i=&lgline,	.di=6,	.h="Cache line size is 2^ =n bytes" },
     { "--ways=i",	.i=&ways,	.di=8,	.h="Cache is =w ways set associativity" },
     { "--sets=i",	.i=&lgsets,	.di=11,	.h="Cache has 2^ =n sets per way" },
     { "--report=i",	.i=&report,	.di=10,	.h="Progress report every =number million instructions" },
     { "--quiet",	.b=&quiet,	.bv=1,	.h="Don't report progress to stderr" },
     { "-q",		.b=&quiet,	.bv=1,	.h="short for --quiet" },
     { 0 }
  };
const char* usage = "cachesim --in=trace [cachesim-options]";

void report_status(long now)
{
  double elapse = (clock() - start_tick) / CLOCKS_PER_SEC;
  fprintf(stderr, "\r%3.1fB cycles %3.1fB refs %5.3f%% miss in %3.1fs for %3.1fM CPS    ", now/1e9, refs/1e9, 100.0*misses/refs, elapse, now/1e6/elapse);
}

int main(int argc, const char** argv)
{
  int numopts = parse_options(argv+1);
  if (!in_path)
    help_exit();
  report *= 1000000;
  cacheData* cache = (cacheData*)newCacheData();
  configureCache(cache, (char*)"Wilson's cache", ways, lgline, lgsets);

  long next_report = report_frequency;
  
  fifo = fifo_open(in_path);
  start_tick = clock();

  long now;
  for (uint64_t tr=fifo_get(fifo); tr!=tr_eof; tr=fifo_get(fifo)) {
    if (tr_code(tr) == tr_time)
      now = tr_cycle(tr);
    else {
      now += tr_delta(tr);
      ++refs;
      char mode = 'r';
      if (tr_code(tr) == tr_exclusive || tr_code(tr) == tr_dirty)
	mode = 'w';
      static cacheWay* way;
      if (!lookup(mode, tr_addr(tr), cache, &way))
	++misses;
    }
    if (refs >= next_report && !quiet) {
      report_status(now);
      next_report += report;
    }
  }
  fprintf(stderr, "\n\n");
  report_status(now);
  fprintf(stderr, "\n\n");
  reportCacheStats(cache);
  printf("\n");
  fifo_close(fifo);
  return 0;
}
