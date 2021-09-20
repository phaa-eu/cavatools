/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

struct perf_header_t {		// performance segment header
  long size;			// of shared memory segment in bytes
  long parcels;			// length of text segment (2B parcels)
  long base;			// address of code segment
  long _cores;			// number of simulated cores allocated
  volatile char arrays[0];	// beginning of dynamic arrays
};

struct count_t {		// perinstruction counters
  volatile long executed;	// number of times executed
  volatile long cycles;		// total number of cycles
};

class perf_t {			// pointers into shared memory structure
  static perf_header_t* h;	// shared segment
  volatile count_t* _count;
  volatile long* _imiss;
  volatile long* _dmiss;
  long index(long pc) { checkif(h->base<=pc && (pc-h->base)/2<h->parcels); return (pc - h->base) / 2; }
public:
  perf_t(long n);		// initialize as core n
  static void create(long base, long bound, long n, const char* shm_name);
  static void open(const char* shm_name);
  static void close(const char* shm_name);
  static long cores() { return h->_cores; }
  long count(long pc) { return _count[index(pc)].executed; }
  long cycle(long pc) { return _count[index(pc)].cycles;   }
  long imiss(long pc) { return _imiss[index(pc)]; }
  long dmiss(long pc) { return _dmiss[index(pc)]; }
  void inc_count( long pc, long k =1) { _count[index(pc)].executed += k; }
  void inc_cycle( long pc, long k =1) { _count[index(pc)].cycles   += k; }
  void inc_imiss( long pc, long k =1) { _imiss[index(pc)] += k; }
  void inc_dmiss( long pc, long k =1) { _dmiss[index(pc)] += k; }
};
