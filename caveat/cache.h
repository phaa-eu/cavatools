/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#ifndef CACHE_T
#define CACHE_T


struct tag_t {			/* 16 byte object */
  long     addr         ;	/* cache line tag */
  unsigned dirty    :  1;	/* line is modified */
  unsigned prefetch :  1;	/* line was prefetched */
  long     ready    : 62;	/* time when line available */
};

struct lru_fsm_t {
  unsigned short way;		/* cache way to look up */
  unsigned short next_state;	/* number if hit */
};

typedef volatile struct {    /* cache descriptor */
  const char* name;		/* for printing */
  struct lru_fsm_t* fsm;	/* LRU state transitions [ways!][ways] */
  long line;			/* line size in bytes */
  long rows;			/* number of rows */
  long ways;			/* number of ways */
  long lg_line, lg_rows;	/* specified in log-base-2 units */
  long tag_mask;		/* = ~((1<<lg_line)-1) */
  long row_mask;		/* row index mask = ((1<<lg_rows)-1) << dc->lg_line */
  struct tag_t** tags;		/* cache tag array [ways]->[rows] */
  unsigned short* states;	/* LRU state vector [rows] */
  long* evicted;		/* tag of evicted line, 0 if clean, NULL if unwritable */
  long penalty;			/* cycles to refill line */
  long refs, misses;	/* count number of */
  long updates, evictions; /* if writeable */
} cache_t;

void flush_cache(cache_t* c);
void init_cache(cache_t* c, const char* name, int penalty, int ways, int lg_line, int lg_rows, int writeable);
void show_cache(cache_t* c);
void print_cache(cache_t* c, FILE* f);


/* returns cycle when line available (may be in past)
     cache miss if return value == when_miss_arrive */
static inline long lookup_cache(cache_t* c, long addr, int write, long when_miss_arrive)
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
