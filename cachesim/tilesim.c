/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "cache.h"
#include "lru_fsm_1way.h"
#include "lru_fsm_2way.h"
#include "lru_fsm_3way.h"
#include "lru_fsm_4way.h"

static const char* in_path;
struct cache_t cache;

struct timeval start_timeval;
long report, quiet;

const struct options_t opt[] =
  {  { "--in=s",	.s=&in_path,		.ds=0,		.h="Trace file =name from caveat" },
     { "--miss=i",	.i=&cache.penalty,	.di=100,	.h="Cache miss =number cycles" },
     { "--line=i",	.i=&cache.lg_line,	.di=6,		.h="Cache line size is 2^ =n bytes" },
     { "--ways=i",	.i=&cache.ways,		.di=4,		.h="Cache is =w ways set associativity" },
     { "--sets=i",	.i=&cache.lg_rows,	.di=12,		.h="Cache has 2^ =n sets per way" },
     { "--report=i",	.i=&report,		.di=100,	.h="Progress report every =number million cycles" },
     { "--quiet",	.b=&quiet,		.bv=1,		.h="Don't report progress to stderr" },
     { "-q",		.b=&quiet,		.bv=1,		.h="short for --quiet" },
     { 0 }
  };
const char* usage = "cachesim --in=trace [cachesim-options]";


int N = 1;

struct trace_t {
  struct fifo_t* fifo;
  long now;
};

struct trace_t* trace;
long now = 0;

void report_status()
{
  struct timeval *t1=&start_timeval, t2;
  gettimeofday(&t2, 0);
  double msec = (t2.tv_sec - t1->tv_sec)*1000;
  msec += (t2.tv_usec - t1->tv_usec)/1000.0;
  fprintf(stderr, "\r%3.1fB cycles %3.1fB refs %5.3f%% miss in %3.1fs for %3.1fM CPS    ",
	  now/1e9, cache.refs/1e9, 100.0*cache.misses/cache.refs, msec/1e3, now/(1e3*msec));
}

void open_traces(const char* names)
{
  char name[1024];
  /* count number of traces */
  for (const char* p=names; *p; p++)
    if (*p == ',')
      N++;
  trace = malloc(N*sizeof *trace);
  for (int i=0; i<N-1; i++) {
    char* p = name;
    while (*names && *names != ',')
      *p++ = *names++;
    *p = 0;
    trace[i].fifo = fifo_open(name);
    names++;
  }
  trace[N-1].fifo = fifo_open(names);
}

int main(int argc, const char** argv)
{
  gettimeofday(&start_timeval, 0);
  
  int numopts = parse_options(argv+1);
  if (!in_path)
    help_exit();
  report *= 1000000;
  /* initialize L2 cache */
  struct lru_fsm_t* fsm;
  switch (cache.ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--ways=1..4 only\n");  exit(-1);
  }
  init_cache(&cache, "L2", fsm, 1);
  //  print_cache(&cache, stdout);
  long next_report = report;
  open_traces(in_path);
  struct cache_t* c = &cache;
  while (1) {
    /* find oldest trace entry */
    struct trace_t* t = 0;	/* points to oldest trace  */
    long oldest = 0x7ffffffffffffffL;
    int finished;
    for (int i=0; i<N; i++)
      if (!fifo_empty(trace[i].fifo)) {
	long tr = fifo_peek(trace[i].fifo);
	if (tr == tr_eof) {
	  finished++;
	  continue;
	}
	long now = tr_code(tr)==tr_time ? tr_cycle(tr) : trace[i].now + tr_delta(tr);
	if (now < oldest) {
	  t = &trace[i];
	  oldest = now;
	}
      }
    if (finished == N)		/* all traces at eof */
      break;
    if (t == 0) {		/* nothing in any trace buffer */
      static const struct timespec nap = { 0, 1000 };
      nanosleep(&nap, 0);
      continue;
    }
    long tr = fifo_get(t->fifo);
    if (tr_code(tr) == tr_time)
      t->now = tr_cycle(tr);
    else {
      t->now += tr_delta(tr);
      assert(t->now >= now);
      now = t->now;
      int write = tr_code(tr) == tr_exclusive || tr_code(tr) == tr_dirty;
      long addr = tr_addr(tr) & c->tag_mask;
      int index = (addr & c->row_mask) >> c->lg_line;
      unsigned short* state = c->states + index;
      struct lru_fsm_t* p = c->fsm + *state; /* recall c->fsm points to [-1] */
      struct lru_fsm_t* end = p + c->ways;	 /* hence +ways = last entry */
      struct tag_t* tag;
      c->refs++;
      do {
	p++;
	tag = c->tags[p->way] + index;
	if (addr == tag->addr)
	  goto cache_hit;
      } while (p < end);
  
      c->misses++;
      if (tag->dirty) {
	//	*c->evicted = tag->addr;	/* will SEGV if not cache not writable */
	c->evictions++;		/* can conveniently point to your location */
	tag->dirty = 0;
      }
      else if (c->evicted)
	*c->evicted = 0;
      tag->addr = addr;
      tag->ready = now+c->penalty;
  
    cache_hit:
      *state = p->next_state;	/* already multiplied by c->ways */
      if (write) {
	tag->dirty = 1;
	c->updates++;
      }
      //      return tag->ready;
    }
    if (now >= next_report && !quiet) {
      report_status();
      next_report += report;
    }
  }
  report_status();
  fprintf(stderr, "\n\n");
  print_cache(&cache, stdout);
  printf("Miss rate %5.3f%% per cycle\n", 100.0*cache.misses/now);
  for (int i=0; i<N; i++)
    fifo_close(trace[i].fifo);
  return 0;
}
