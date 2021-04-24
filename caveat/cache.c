#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"

void flush_cache(struct cache_t* c)
{
  for (int k=0; k<c->ways; k++)
    memset((char*)c->tags[k], 0, c->ways*sizeof(struct tag_t));
  memset((char*)c->states, 0, c->rows*sizeof(unsigned short));
}

void init_cache(struct cache_t* c, const char* name, struct lru_fsm_t* fsm, int writeable)
{
  c->name = name;
  c->fsm = fsm;			/* note purposely point to [-1] */
  c->line = 1 << c->lg_line;
  c->rows = 1 << c->lg_rows;
  c->ways = fsm->way;
  c->tag_mask = ~(c->line-1);
  c->row_mask =  (c->rows-1) << c->lg_line;
  c->tags = (struct tag_t**)malloc(c->ways*sizeof(struct tag_t**));
  for (int k=0; k<c->ways; k++)
    c->tags[k] = (struct tag_t*)malloc(c->rows*sizeof(struct tag_t));
  c->states = (unsigned short*)malloc(c->rows*sizeof(unsigned short));
  flush_cache(c);
  static long place =0;
  c->evicted = writeable ? &place : 0;
  c->refs = c->misses = 0;
  c->updates = c->evictions = 0;
}


void show_cache( struct cache_t* c )
{
  fprintf(stderr, "lg_line=%ld lg_rows=%ld line=%ld rows=%ld ways=%ld row_mask=0x%lx\n",
	  c->lg_line, c->lg_rows, c->line, c->rows, c->ways, c->row_mask);
}

void print_cache(struct cache_t* c, FILE* f)
{
  fprintf(f, "%s cache\n", c->name);
  long size = c->line * c->rows * c->ways;
  if      (size >= 1024*1024)  fprintf(f, "  %3.1f MB capacity\n", size/1024.0/1024);
  else if (size >=      1024)  fprintf(f, "  %3.1f KB capacity\n", size/1024.0);
  else                         fprintf(f, "  %ld B capacity\n", size);
  fprintf(f, "  %ld bytes line size\n", c->line);
  fprintf(f, "  %ld ways set associativity\n", c->ways);
  fprintf(f, "  %ld references\n", c->refs);
  fprintf(f, "  %ld stores (%5.3f%%)\n", c->updates, 100.0*c->updates/c->refs);
  fprintf(f, "  %ld misses (%5.3f%%)\n", c->misses, 100.0*c->misses/c->refs);
  fprintf(f, "  %ld writebacks (%5.3f%%)\n", c->evictions, 100.0*c->evictions/c->refs);
}
  
