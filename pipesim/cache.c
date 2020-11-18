#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"

void flush_cache( struct cache_t* c )
{
  for (int k=0; k<c->ways; k++)
    memset((char*)c->tags[k], 0, c->ways*sizeof(struct tag_t));
  memset((char*)c->states, 0, c->rows*sizeof(unsigned short));
}

void init_cache( struct cache_t* c, int lg_line_size, int lg_rows_per_way, struct lru_fsm_t* fsm, int writeable )
{
  c->fsm = fsm;			/* note purposely point to [-1] */
  c->lg_line = lg_line_size;
  c->lg_rows = lg_rows_per_way;
  c->line = 1 << c->lg_line;
  c->rows = 1 << c->lg_rows;
  c->ways = fsm->way;
  c->row_mask = c->rows-1;
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
  fprintf(stderr, "lg_line=%d lg_rows=%d line=%d rows=%d ways=%d row_mask=0x%lx\n",
	  c->lg_line, c->lg_rows, c->line, c->rows, c->ways, c->row_mask);
}

