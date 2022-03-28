/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#ifndef CACHE_T
#define CACHE_T

class cache_t {
  long _rows;			// number of rows 
  long _line;			// line size in bytes
  long tag_mask;		// = ~(_line-1) 
  long row_mask;		// =  (_rows-1)
  long _refs, _misses;		// count number of
  long _updates, _evictions;	// if writeable
  long lg_line, lg_rows;	// specified in log-base-2 units 
  const char* _name;		// for printing
  template<int, bool, bool> friend class fsm_cache;
public:
  virtual void flush() =0;
  virtual long fetch(long addr) =0;
  virtual long update(long addr) =0;
  virtual long miss_model(long addr, bool exclusive =false) { return 0; }
  virtual long exclusive_model(long addr) { return 0; }
  virtual long eviction_model(long addr) { return 0; }
  cache_t(int line2, int rows2, const char* namep) {
    _name=namep; lg_line=line2; lg_rows=rows2;
    _line=1<<lg_line; _rows=1<<lg_rows;
    tag_mask=~(_line-1); row_mask=_rows-1;
    _refs=_misses=_updates=_evictions=0;
  }
  const char* name() { return _name; }
  virtual long ways() =0;
  long rows() { return _rows; }
  long linesize() { return _line; }
  long linemask() { return tag_mask << lg_line; }
  long refs() { return _refs; }
  long misses() { return _misses; }
  long updates() { return _updates; }
  long evictions() { return _evictions; }
};

typedef unsigned short fsm_state_t;

struct lru_fsm_t {
  unsigned short way;		// cache way to look up 
  fsm_state_t next_state;	// number if hit 
};
static_assert(sizeof(lru_fsm_t) == 4);

extern lru_fsm_t* lru_fsm_table[];

template<int _ways =4, bool writeable =true, bool prefetch =false>
class fsm_cache : public cache_t {

  class fsm_tag {
    unsigned long v;
  public:
    fsm_tag()		{ }
    fsm_tag(long addr)	{ v = addr << 2;   }
    long key()		{ return v >> 2;   }
    bool dirty()	{ return v & 0x1L; }
    bool seen()		{ return v & 0x2L; }
    void smudge()	{ v |=       0x1L; }
    void saw()		{ v |=       0x2L; }
  };
  
  fsm_tag *tags;		// cache tag array [rows*ways]
  fsm_state_t* states;		// LRU state vector [rows] 
  lru_fsm_t* fsm;		// LRU state transitions [ways!][ways]

  long lookup(long addr, bool iswrite) {
    _refs++;
    addr >>= lg_line; // make proper tag (ok to include index)
    long index = addr & row_mask;
    fsm_state_t* state = &states[index];
    lru_fsm_t* p = fsm + *state;	// fsm points to [-1]
    lru_fsm_t* end = p + ways();	// points to last entry
    fsm_tag* tag = &tags[index*ways()];	// tags is 2D array
    long delay = -1;			// signify hit
    do {
      p++;
      if (tag[p->way].key() == addr)
	goto cache_hit;
    } while (p < end);
    _misses++;
    delay = miss_model(addr << lg_line, iswrite);
    if (writeable && tag[p->way].dirty()) {
      _evictions++;
      delay += eviction_model(tag[p->way].key() << lg_line);
    }
    tag[p->way] = fsm_tag(addr);
  cache_hit:
    *state = p->next_state;
    if (iswrite) {
      _updates++;
      tag[p->way].smudge();
    }
    if (!prefetch || tag[p->way].seen())
      return delay;
    tag[p->way].saw();
    addr += 1;		// prefetch next line
    index = (index+1) & row_mask;
    state = &states[index];
    p = fsm + *state;		// fsm points to [-1]
    end = p + ways();
    tag = &tags[index*ways()];
    do {
      p++;
      if (tag[p->way].key() == addr)
	goto prefetch_hit;
    } while (p < end);
    if (writeable && tag[p->way].dirty()) {
      _evictions++;
      delay += eviction_model(tag[p->way].key());
    }
    tag[p->way] = fsm_tag(addr);
  prefetch_hit:
    *state = p->next_state;
    return delay;
  }
  
public:
  long ways() { return _ways; }
  long fetch( long addr) { return lookup(addr, false); }
  long update(long addr) { return lookup(addr, true ); }
  void flush() {
    memset(tags, 0, rows()*ways()*sizeof(fsm_tag));
    memset(states, 0, rows()*sizeof(fsm_state_t));
  }
  fsm_cache(int line2, int rows2, const char* name) : cache_t(line2, rows2, name) {
    fsm = lru_fsm_table[ways()]; //note fsm purposely point to state=-1
    tags = new fsm_tag[rows()*ways()];
    states = new fsm_state_t[rows()];
    flush();
  }
};

#endif
