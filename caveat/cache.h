/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#ifndef CACHE_T
#define CACHE_T


struct tag_t {			// 16 byte object 
  long     addr         ;	// cache line tag 
  unsigned dirty    :  1;	// line is modified 
  unsigned prefetch :  1;	// line was prefetched 
  long     ready    : 62;	// time when line available 
};

struct lru_fsm_t {
  unsigned short way;		// cache way to look up 
  unsigned short next_state;	// number if hit 
};

class cache_t {		        // cache descriptor 
  const char* name;		// for printing 
  struct lru_fsm_t* fsm;	// LRU state transitions [ways!][ways] 
  long line;			// line size in bytes 
  long rows;			// number of rows 
  long ways;			// number of ways 
  long lg_line, lg_rows;	// specified in log-base-2 units 
  long tag_mask;		// = ~((1<<lg_line)-1) 
  long row_mask;		// row index mask = ((1<<lg_rows)-1) << dc->lg_line 
  tag_t** tags;			// cache tag array [ways][rows]
  unsigned short* states;	// LRU state vector [rows] 
  long* evicted;		// tag of evicted line, 0 if clean, NULL if unwritable 
  long penalty;			// cycles to refill line 
  long _refs, _misses;		// count number of 
  long _updates, _evictions;	// if writeable
  
 public:
  cache_t(const char* nam, int miss, int w, int lin, int row, bool writeable);
  long lookup(long addr, int write =false);
  long refs() { return _refs; }
  long misses() { return _misses; }
  long updates() { return _updates; }
  long evictions() { return _evictions; }
  
  void flush();
  void show();
  void print(FILE* f =stderr);
};

/* returns cycle when line available (may be in past)
   cache miss if return value == when_miss_arrive */
#define when_miss_arrive 10
inline long cache_t::lookup(long addr, int write)
{
  _refs++;
  addr >>= lg_line;		// make proper tag (ok to include index) 
  int index = addr & row_mask;
  unsigned short* state = states + index;
  
  struct lru_fsm_t* p = fsm + *state; // recall fsm points to [-1] 
  struct lru_fsm_t* end = p + ways;	 // hence +ways = last entry 
  struct tag_t* tag;
  do {
    p++;
    tag = tags[p->way] + index;
    //    tag = tags + index*ways + p->way;
    if (addr == tag->addr)
      goto cache_hit;
  } while (p < end);
  
  _misses++;
  if (tag->dirty) {
    *evicted = tag->addr;	// will SEGV if not cache not writable 
    _evictions++;		// can conveniently point to your location 
    tag->dirty = 0;
  }
  else if (evicted)
    *evicted = 0;
  tag->addr = addr;
  tag->ready = when_miss_arrive;
  
 cache_hit:
  *state = p->next_state;	// already multiplied by ways 
  if (write) {
    tag->dirty = 1;
    _updates++;
  }
  return tag->ready;
}


void flush_cache();
void init_cache();
void show_cache();
void print_cache(FILE* f =stderr);


#endif
