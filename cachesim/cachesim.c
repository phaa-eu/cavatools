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


#define REPORT_FREQUENCY  10000000

#define RT_READ		0b0000000000000010L
#define RT_WRITE	0b0000000000000100L
#define RT_LDST		0b0000000000001000L
#define RT_GETPUT	0b0000000000010000L
#define RT_LEVEL_SHIFT	5
#define RT_L0_CACHE	0b0000000000100000L
#define RT_L1_CACHE	0b0000000001000000L
#define RT_L2_CACHE	0b0000000010000000L
#define RT_L3_CACHE	0b0000000100000000L
#define RT_INSN_CACHE	0b0000001000000000L
#define RT_DATA_CACHE	0b0000010000000000L



struct fifo_t fifo;
struct fifo_t outbuf;
long report_frequency = REPORT_FREQUENCY;

void allocError(uint64 n, char * thing, char * filename, int32 linenumber)
{
  printf("\nmemory allocation failed trying to allocate %lld bytes for a '%s' on line %d in file %s",
	 n, thing, linenumber, filename);
}

int main(int argc, const char** argv)
{
  static const char* in_path =0;
  static const char* out_path =0;
  static const char* ways = "2";
  static const char* lgline = "6";
  static const char* lgsets = "6";
  static const char* filter_flags =0;
  static const char* report =0;
  static struct options_t flags[] =
    {  { "--in=",	.v = &in_path		},
       { "--out=",	.v = &out_path		},
       { "--ways=",	.v = &ways		},
       { "--line=",	.v = &lgline		},
       { "--sets=",	.v = &lgsets		},
       { "--filter=",	.v = &filter_flags	},
       { "--report=",	.v = &report		},
       { 0					}
    };
  int numopts = parse_options(flags, argv+1);
  if (!in_path || !ways || !lgline || !lgsets) {
    fprintf(stderr, "usage:  cachesim --in=shm --ways=numways, --line=log2LineLength, --sets=log2Lines\n");
    fprintf(stderr, "--filter=[0123idrwIDRW] union of cache type/levels and load/store, default all\n");
    exit(0);
  }
  cacheData* cache = (cacheData*)newCacheData();
  configureCache(cache, (char*)"Wilson's cache", atoi(ways), atoi(lgline), atoi(lgsets));

  long insns=0, refs=0, misses=0;
  if (report)
    report_frequency = atoi(report);
  long next_report = report_frequency;
  long filter = 0L;
  if (filter_flags) {
    filter |= strchr(filter_flags, '0') ? RT_L0_CACHE : 0;
    filter |= strchr(filter_flags, '1') ? RT_L1_CACHE : 0;
    filter |= strchr(filter_flags, '2') ? RT_L3_CACHE : 0;
    filter |= strchr(filter_flags, '3') ? RT_L3_CACHE : 0;
    filter |= strchr(filter_flags, 'i') ? RT_INSN_CACHE : 0;
    filter |= strchr(filter_flags, 'I') ? RT_INSN_CACHE : 0;
    filter |= strchr(filter_flags, 'd') ? RT_DATA_CACHE : 0;
    filter |= strchr(filter_flags, 'D') ? RT_DATA_CACHE : 0;
    filter |= strchr(filter_flags, 'r') ? RT_LDST : 0;
    filter |= strchr(filter_flags, 'R') ? RT_LDST : 0;
    filter |= strchr(filter_flags, 'w') ? RT_LDST : 0;
    filter |= strchr(filter_flags, 'W') ? RT_LDST : 0;
    filter |= strchr(filter_flags, 'r') ? RT_READ : 0;
    filter |= strchr(filter_flags, 'R') ? RT_READ : 0;
    filter |= strchr(filter_flags, 'w') ? RT_WRITE : 0;
    filter |= strchr(filter_flags, 'W') ? RT_WRITE : 0;
  }
  else
    filter = ~0L;		/* default simulate all references */
    
  fifo_init(&fifo, in_path, 1);
  if (out_path)
    fifo_init(&outbuf, out_path, 0);
  clock_t start_tick = clock();
  for (uint64_t tr=fifo_get(&fifo); tr_code(tr)!=tr_eof; tr=fifo_get(&fifo)) {
    if (tr_code(tr) == tr_icount) {
      insns = tr_value(tr);
      if (out_path)
	fifo_put(&outbuf, tr);
      continue;
    }
    if (!is_mem(tr))
      continue;
    long reftype = is_store(tr) ? RT_WRITE : RT_READ;
    if (is_ldst(tr))
      reftype |= RT_LDST;
    else {
      reftype |= RT_GETPUT;
      reftype |= (1L<<tr_clevel(tr)) << RT_LEVEL_SHIFT;
    }
    if (reftype & filter) {
      cacheWay* way;
      ++refs;
      if (!lookup(is_write(tr)?'w':'r', tr_value(tr), cache, &way)) {
	++misses;
	if (out_path)
	  fifo_put(&outbuf, tr);
      }
      if (refs >= next_report) {
	double elapse = (clock() - start_tick) / CLOCKS_PER_SEC;
	fprintf(stderr, "\r%3.1fB insns %3.1fB refs %3.1f misses/Kinsns in %3.1fs for %3.1f MIPS    ", insns/1e9, refs/1e9, misses/(insns/1e3), elapse, insns/1e6/elapse);
	next_report += REPORT_FREQUENCY;
      }
    }
    else if (out_path)		/* pass to next stage */
      fifo_put(&outbuf, tr);
  }
  fprintf(stderr, "\n\n");
  reportCacheStats(cache);
  printf("\n");
  if (out_path) {
    fifo_put(&outbuf, trM(tr_eof, 0));
    fifo_fini(&outbuf);
  }
  fifo_fini(&fifo);
  return 0;
}
