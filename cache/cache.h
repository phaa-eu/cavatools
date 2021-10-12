/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#ifndef CACHE_T
#define CACHE_T

struct lru_fsm_t {
  unsigned short way;		// cache way to look up 
  unsigned short next_state;	// number if hit 
};
static_assert(sizeof(lru_fsm_t) == 4);

class tag_t {
  bool _dirty  :  1;
  bool _unseen :  1;
  long _ready  : 62;
  long	_addr;
public:
  tag_t() { }
  tag_t(long addr, long ready, bool unseen) { _addr=addr; _dirty=false; _unseen=unseen; _ready=ready; }
  long addr() { return _addr; }
  long ready() { return _ready; }
  bool dirty() { return _dirty; }
  void modify() { _dirty=true; }
  bool touched() { return !_unseen; }
  void touch() { _unseen=false; }
};
static_assert(sizeof(tag_t) == 16);

class cache_t {			// cache descriptor 
  tag_t* tags;			// cache tag array [rows*ways]
  unsigned short* states;	// LRU state vector [rows] 
  struct lru_fsm_t* fsm;	// LRU state transitions [ways!][ways]
  long _ways;			// number of ways 
  long _rows;			// number of rows 
  long _line;			// line size in bytes 
  long lg_line, lg_rows;	// specified in log-base-2 units 
  long tag_mask;		// = ~(_line-1) 
  long row_mask;		// =  (_rows-1)
  long _refs, _misses;		// count number of
  long _updates, _evictions;	// if writeable
  long* evicted;		// tag of evicted line, tag==0 if clean, NULL if unwritable
  long place =0;		// where evicted points to
  const char* _name;		// for printing
  bool _prefetch;		// next-line prefetch on/off

public:
  cache_t(const char* name, int ways, int line, int rows, bool writeable, bool prefetch =false);
  const char* name() { return _name; }
  long line() { return _line; }
  long rows() { return _rows; }
  long ways() { return _ways; }
  long refs() { return _refs; }
  long hits() { return _refs-_misses; }
  long misses() { return _misses; }
  long updates() { return _updates; }
  long evictions() { return _evictions; }
  long linemask(long pc) { return pc & tag_mask; }
  long linesize() { return _line; }
  bool prefetch() { return _prefetch; }
  void flush();
  void print(FILE* f =stderr);

  virtual lru_fsm_t* modify_way(lru_fsm_t* p, long addr, long miss_ready, bool &prefetch) { return p; }
  
  long lookup(long addr, bool iswrite, long miss_ready) {
    _refs++;
    addr >>= lg_line;	      // make proper tag (ok to include index)
    int index = addr & row_mask;
    unsigned short* state = states + index;
    lru_fsm_t* p = fsm + *state; // fsm points to [-1]
    lru_fsm_t* end = p + ways(); // hence +ways = last
    tag_t* tag = &tags[index * ways()];	// tag[index][way] in 1D
    bool pf = prefetch();
    do if (tag[(++p)->way].addr() == addr)
	 goto cache_hit;
    while (p < end);
    _misses++;
    p = modify_way(p, addr, miss_ready, pf);
    if (evicted && tag[p->way].dirty()) {
      *evicted = tag[p->way].addr();
      _evictions++;
    }
    tag[p->way] = tag_t(addr, miss_ready, pf);
  cache_hit:
    *state = p->next_state;
    if (iswrite)
      tag[p->way].modify();
    // touched always true if not prefetching
    if (tag[p->way].touched())
      return tag[p->way].ready();
    tag[p->way].touch();
    long ready = tag[p->way].ready();
    // prefetch next line
    addr += 1;
    index = (index+1) & row_mask;
    state = states + index;
    p = fsm + *state; // fsm points to [-1]
    end = p + ways(); // hence +ways = last
    tag = &tags[index * ways()];	// tag[index][way] in 1D
    do if (tag[(++p)->way].addr() == addr)
	 return ready;
    while (p < end);
    p = modify_way(p, addr, miss_ready, pf);
    if (evicted && tag[p->way].dirty()) {
      *evicted = tag[p->way].addr();
      _evictions++;
    }
    tag[p->way] = tag_t(addr, miss_ready+1, true);
    return ready;
  }    
};

#endif
