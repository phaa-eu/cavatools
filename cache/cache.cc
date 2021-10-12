#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

#include "cache.h"
#include "lru_fsm_1way.h"
#include "lru_fsm_2way.h"
#include "lru_fsm_3way.h"
#include "lru_fsm_4way.h"

cache_t::cache_t(const char* name_, int ways_, int line_, int rows_, bool writeable, bool prefetch_)
{
  _name = name_;
  lg_line = line_;
  lg_rows = rows_;
  _line = 1 << lg_line;
  _rows = 1 << lg_rows;
  _ways = ways_;
  switch (ways()) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:
    fprintf(stderr, "ways=%ld only 1..4 ways implemented\n", ways());
    syscall(SYS_exit_group, -1);
  } /* note fsm purposely point to [-1] */
  tag_mask = ~(line()-1);
  row_mask =   rows()-1;
  tags = new tag_t[rows()*ways()];
  states = new unsigned short[rows()];
  flush();
  _refs = _misses = 0;
  _updates = _evictions = 0;
  evicted = writeable ? &place : 0;
  _prefetch = prefetch_;
  //fprintf(stderr, "created %s cache: line=%ld ways=%ld rows=%ld\n", name(), line(), ways(), rows());
}

void cache_t::flush()
{
  memset(tags, 0, rows()*ways()*sizeof(tag_t));
  memset(states, 0, rows()*sizeof(unsigned short));
}

void cache_t::print(FILE* f)
{
  fprintf(f, "%s cache\n", name());
  long size = line() * rows() * ways();
  if      (size >= 1024*1024)  fprintf(f, "  %3.1f MB capacity\n", size/1024.0/1024);
  else if (size >=      1024)  fprintf(f, "  %3.1f KB capacity\n", size/1024.0);
  else                         fprintf(f, "  %ld B capacity\n", size);
  fprintf(f, "  %ld bytes line size\n", line());
  fprintf(f, "  %ld ways set associativity\n", ways());
  fprintf(f, "  %ld references\n", _refs);
  fprintf(f, "  %ld misses (%5.3f%%)\n", _misses, 100.0*_misses/_refs);
  if (evicted) {
    fprintf(f, "  %ld stores (%5.3f%%)\n", _updates, 100.0*_updates/refs());
    fprintf(f, "  %ld writebacks (%5.3f%%)\n", _evictions, 100.0*_evictions/refs());
  }    
}
  
