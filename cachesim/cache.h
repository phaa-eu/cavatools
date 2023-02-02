/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#ifndef CACHE_T
#define CACHE_T

struct cache_t {			// cache descriptor 
  const char* name;		// for printing 
  long line;			// line size in bytes 
  long rows;			// number of rows 
  long ways;			// number of ways 
  long lg_line, lg_rows;	// specified in log-base-2 units
  long tag_mask;		// = ~((1<<lg_line)-1) 
  long row_mask;		// row index mask = ((1<<lg_rows)-1) << dc->lg_line 
  long _refs, _misses;		// count number of 
  long _updates, _evictions;	// if writeable
  long* evicted;		// tag of evicted line, 0 if clean, NULL if unwritable 

  cache_t(const char* nam, int w, int lin, int row, bool writeable);
  long refs() { return _refs; }
  long misses() { return _misses; }
  long updates() { return _updates; }
  long evictions() { return _evictions; }
  void print(FILE* f =stderr);
  
  virtual bool lookup(long addr, bool write =false) =0;
  virtual void flush() =0;
};



struct lru_fsm_t {
  unsigned short way;		// cache way to look up 
  unsigned short next_state;	// number if hit 
};

struct fsm_tag_t {
  bool dirty : 1;
  long addr : 63;
};

class fsm_cache_t : public cache_t {
  struct lru_fsm_t* fsm;	// LRU state transitions [ways!][ways] 
  fsm_tag_t** tags;			// cache tag array [ways][rows]
  unsigned short* states;	// LRU state vector [rows]
public:  
  fsm_cache_t(const char* nam, int w, int lin, int row, bool writeable);
  bool lookup(long addr, bool write =false);
  void flush();
  void print(FILE* f =stderr);
};



struct ll_tag_t {
  ll_tag_t *next;		// from MRU to LRU
  union {
    long bits;
    struct {
      bool dirty	:  1;
      long addr		: 63;
    } tag;
  };
};

class ll_cache_t : public cache_t {
  ll_tag_t* tags;		// 2D array of tags
  ll_tag_t** mru;		// most recently used tag in row
public:
  ll_cache_t(const char* nam, int w, int lin, int row, bool writeable);
  bool lookup(long addr, bool write =false);
  void flush();
};




inline bool fsm_cache_t::lookup(long addr, bool write)
{
  _refs++;
  addr >>= lg_line;		// make proper tag (ok to include index) 
  int index = addr & row_mask;
  unsigned short* state = states + index;
  struct lru_fsm_t* p = fsm + *state; // recall fsm points to [-1] 
  struct lru_fsm_t* end = p + ways;	 // hence +ways = last entry 
  struct fsm_tag_t* tag;
  bool hit = true;
  do {
    p++;
    tag = tags[p->way] + index;
    if (addr == tag->addr)
      goto cache_hit;
  } while (p < end);
  hit = false;
  tag->addr = addr;
  _misses++;
  if (tag->dirty) {
    *evicted = tag->addr;	// will SEGV if not cache not writable 
    _evictions++;		// can conveniently point to your location 
    tag->dirty = 0;
  }
  else if (evicted)
    *evicted = 0;
  tag->addr = addr;
  
 cache_hit:
  *state = p->next_state;	// already multiplied by ways
  if (write) {
    tag->dirty = 1;
    _updates++;
  }
  return hit;
}




inline bool ll_cache_t::lookup(long addr, bool write)
{
  _refs++;
  addr >>= lg_line;		// make proper tag (ok to include index) 
  int index = addr & row_mask;
  int row = index * ways;
  bool hit = true;
  ll_tag_t* p = mru[index];
  ll_tag_t* q = 0;
  while (p) {
    if (addr == p->tag.addr) {
      if (q) {			// if q==0 p points to MRU
	q->next = p->next;
	p->next = mru[index];
	mru[index] = p;
      }
      goto cache_hit;
    }
    q = p;
    p = p->next;
  }
  _misses++;
  q = 0;			// find and unlink last element
  p = mru[index];
  while (p->next) {
    q = p;
    p = p->next;
  } // p points to last element, q to element before
  if (q) {			// unlink p only if
    q->next = p->next;		//   not MRU
    p->next = mru[index];	// otherwise do
    mru[index] = p;		//   nothing
  }
  if (p->tag.dirty) {
    *evicted = p->tag.addr;
    _evictions++;
    p->tag.dirty = 0;
  }
  else if (evicted)
    *evicted = 0;
  p->tag.addr = addr;

 cache_hit:
  if (write) {
    p->tag.dirty = 1;
    _updates++;
  }
  return hit;
}



cache_t* new_cache(const char* nam, int w, int lin, int row, bool writeable);


#endif
