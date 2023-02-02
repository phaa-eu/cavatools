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
#include "lru_fsm_5way.h"
#include "lru_fsm_6way.h"

cache_t::cache_t(const char* nam, int w, int lin, int row, bool writeable)
{
  name = nam;
  ways = w;
  lg_line = lin;
  lg_rows = row;
  line = 1 << lg_line;
  rows = 1 << lg_rows;
  tag_mask = ~(line-1);
  row_mask =  (rows-1);
  _refs = _misses = 0;
  _updates = _evictions = 0;
  static long place =0;
  evicted = writeable ? &place : 0;
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
  fprintf(f, "  %ld references\n", _refs);
  fprintf(f, "  %ld misses (%5.3f%%)\n", _misses, 100.0*_misses/_refs);
  if (evicted) {
    fprintf(f, "  %ld stores (%5.3f%%)\n", _updates, 100.0*_updates/_refs);
    fprintf(f, "  %ld writebacks (%5.3f%%)\n", _evictions, 100.0*_evictions/_refs);
  }
}



fsm_cache_t::fsm_cache_t(const char* nam, int w, int lin, int row, bool writeable)
  : cache_t(nam, w, lin, row, writeable)
{
  switch (ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  case 5:  fsm = cache_fsm_5way;  break;
  case 6:  fsm = cache_fsm_6way;  break;
  default:
    fprintf(stderr, "fsm_cache_t supports only 1..6 ways, not %ld\n", ways);
    syscall(SYS_exit_group, -1);
  } /* note fsm purposely point to [-1] */
  tags = new fsm_tag_t*[ways];
  for (int k=0; k<ways; k++)
    tags[k] = new fsm_tag_t[rows];
  states = new unsigned short[rows];
  flush();
}

void fsm_cache_t::flush()
{
  for (int k=0; k<ways; k++)
    memset((char*)tags[k], 0, rows*sizeof(long));
  memset((char*)states, 0, rows*sizeof(unsigned short));
}




ll_cache_t::ll_cache_t(const char* nam, int w, int lin, int row, bool writeable)
  : cache_t(nam, w, lin, row, writeable)
{
  mru = new ll_tag_t*[rows];
  tags = new ll_tag_t[rows*ways];
  memset((char*)tags, 0, rows*ways*sizeof(ll_tag_t));
  for (int i=0; i<rows; i++) {
    row = i * ways;
    mru[i] = &tags[row];
    for (int j=0; j<ways-1; j++)
      tags[row+j].next = &tags[row+j+1];
  }
  flush();
}

void ll_cache_t::flush()
{
  for (int i=0; i<rows; i++) {
    int row = i * ways;
    for (ll_tag_t* p=mru[i]; p; p=p->next)
      p->bits = 0;
  }
}






cache_t* new_cache(const char* nam, int w, int lin, int row, bool writeable)
{
  if (w <= 6)
    return new fsm_cache_t(nam, w, lin, row, writeable);
  else
    return new ll_cache_t(nam, w, lin, row, writeable);
}
