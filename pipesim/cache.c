#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"

void flush_cache( struct cache_t* c )
{
  for (int k=0; k<c->ways; k++)
    memset((char*)c->tags[k], 0, c->rows*sizeof(struct tag_t));
  memset((char*)c->states, 0, c->rows*sizeof(unsigned short));
}

void init_cache( struct cache_t* c, struct lru_fsm_t* fsm, int writeable )
{
  fprintf(stderr, "lg_line=%ld, lg_rows=%ld\n", c->lg_line, c->lg_rows);
  c->fsm = fsm;			/* note purposely point to [-1] */
  c->line = 1 << c->lg_line;
  c->rows = 1 << c->lg_rows;
  c->ways = fsm->way;
  fprintf(stderr, "line=%ld, rows=%ld\n", c->line, c->rows);
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


void show_cache( struct cache_t* c, const char* name, long n, FILE* f )
{
  //  fprintf(f, "lg_line=%ld lg_rows=%ld line=%ld rows=%ld ways=%ld row_mask=0x%lx\n",
  //	  c->lg_line, c->lg_rows, c->line, c->rows, c->ways, c->row_mask);

  long size = c->line * c->rows * c->ways;
  if (size >= 1024)
    fprintf(f, "%s %ldB linesize %gKB capacity %ld way\n", name, c->line, size/1024.0, c->ways);
  else
    fprintf(f, "%s %ldB linesize %ldB capacity %ld way\n", name, c->line, size, c->ways);
  long reads = c->refs-c->updates;
  fprintf(f, "%s %12ld cache reads (%3.1f%%)\n", name, reads, 100.0*reads/n);
  fprintf(f, "%s %12ld cache writes (%3.1f%%)\n", name, c->updates, 100.0*c->updates/n);
  fprintf(f, "%s %12ld cache misses (%5.3f%%)\n", name, c->misses, 100.0*c->misses/n);
  if (c->evicted)
    fprintf(f, "%s %12ld cache evictions (%5.3f%%)\n", name, c->evictions,  100.0*c->evictions/n);
  
}

