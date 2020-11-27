/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#ifndef CACHE_T
#define CACHE_T


struct tag_t {			/* 16 byte object */
  long      addr     ;		/* 64-bit entry */
  unsigned dirty :  1;		/*  1 bit flag */
  long     ready : 63;		/* 63 bit time */
};

struct lru_fsm_t {
  unsigned short way;		/* cache way to look up */
  unsigned short next_state;	/* number if hit */
};

struct cache_t {		/* cache descriptor */
  struct lru_fsm_t* fsm;	/* LRU state transitions [ways!][ways] */
  int line;			/* line size in bytes */
  int rows;			/* number of rows */
  int ways;			/* number of ways */
  int lg_line, lg_rows;		/* specified in log-base-2 units */
  long row_mask;		/* row index mask = (1<<lg_rows)-1 */
  struct tag_t** tags;		/* cache tag array [ways]->[rows] */
  unsigned short* states;	/* LRU state vector [rows] */
  long* evicted;		/* tag of evicted line, 0 if clean, NULL if unwritable */
  long refs, misses;		/* count number of */
  long updates, evictions;	/* if writeable */
};

void flush_cache( struct cache_t* c );

void init_cache( struct cache_t* c, int lg_line_size, int lg_rows_per_way, struct lru_fsm_t* fsm, int writeable );

void show_cache( struct cache_t* c );


/* returns cycle when line available (may be in past)
     cache miss if return value == when_miss_arrive */
static inline long lookup_cache( struct cache_t* c, long addr, int write, long when_miss_arrive )
{
  c->refs++;
  addr >>= c->lg_line;		/* make proper tag (ok to include index) */
  int index = addr & c->row_mask;
  unsigned short* state = c->states + index;
  
  struct lru_fsm_t* p = c->fsm + *state; /* recall c->fsm points to [-1] */
  struct lru_fsm_t* end = p + c->ways;	 /* hence +ways = last entry */
  struct tag_t* tag;
  do {
    p++;
    tag = c->tags[p->way] + index;
    if (addr == tag->addr)
      goto cache_hit;
  } while (p < end);
  
  c->misses++;
  if (tag->dirty) {
    *c->evicted = tag->addr;	/* will SEGV if not cache not writable */
    c->evictions++;		/* can conveniently point to your location */
    tag->dirty = 0;
  }
  else if (c->evicted)
    *c->evicted = 0;
  tag->addr = addr;
  tag->ready = when_miss_arrive;
  
 cache_hit:
  *state = p->next_state;	/* already multiplied by c->ways */
  if (write) {
    tag->dirty = 1;
    c->updates++;
  }
  return tag->ready;
}



#endif
