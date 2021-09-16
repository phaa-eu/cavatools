#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

#include "cache.h"
#include "lru_fsm_1way.h"
#include "lru_fsm_2way.h"
#include "lru_fsm_3way.h"
#include "lru_fsm_4way.h"

cache_t::cache_t(const char* nam, int miss, int w, int lin, int row, bool writeable)
{
  name = nam;
  _penalty = miss;
  ways = w;
  switch (ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:
    fprintf(stderr, "ways=%ld only 1..4 ways implemented\n", ways);
    syscall(SYS_exit_group, -1);
  } /* note fsm purposely point to [-1] */
  lg_line = lin;
  lg_rows = row;
  line = 1 << lg_line;
  rows = 1 << lg_rows;
  tag_mask = ~(line-1);
  //  row_mask =  (rows-1) << lg_line;
  row_mask =  (rows-1);
  tags = new long*[ways];
  for (int k=0; k<ways; k++)
    tags[k] = new long[rows];
  states = new unsigned short[rows];
  flush();
  static long place =0;
  evicted = writeable ? &place : 0;
  _refs = _misses = 0;
  _updates = _evictions = 0;
}


void cache_t::flush()
{
  for (int k=0; k<ways; k++)
    memset((char*)tags[k], 0, rows*sizeof(long));
  memset((char*)states, 0, rows*sizeof(unsigned short));
}

void cache_t::show()
{
  fprintf(stderr, "lg_line=%ld lg_rows=%ld line=%ld rows=%ld ways=%ld row_mask=0x%lx\n",
	  lg_line, lg_rows, line, rows, ways, row_mask);
}

void cache_t::print(FILE* f)
{
  fprintf(f, "%s cache\n", name);
  long size = line * rows * ways;
  if      (size >= 1024*1024)  fprintf(f, "  %3.1f MB capacity\n", size/1024.0/1024);
  else if (size >=      1024)  fprintf(f, "  %3.1f KB capacity\n", size/1024.0);
  else                         fprintf(f, "  %ld B capacity\n", size);
  fprintf(f, "  %ld bytes line size\n", line);
  fprintf(f, "  %ld ways set associativity\n", ways);
  fprintf(f, "  %ld cycles miss penalty\n", _penalty);
  fprintf(f, "  %ld references\n", _refs);
  fprintf(f, "  %ld misses (%5.3f%%)\n", _misses, 100.0*_misses/_refs);
#if 0
  if (evicted)
    fprintf(f, "  %ld stores (%5.3f%%)\n", _updates, 100.0*_updates/_refs);
  if (evicted)
    fprintf(f, "  %ld writebacks (%5.3f%%)\n", _evictions, 100.0*_evictions/_refs);
#endif
}
  
