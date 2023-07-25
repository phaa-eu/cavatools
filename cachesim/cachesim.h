
#include "cache.h"

class Counters_t {
  uint64_t* array;		// counters parallel tcache
  Counters_t* next;		// so tcache can find us
  friend class Tcache_t;
public:
  Counters_t() { array=new uint64_t[tcache.extent()]; next=tcache.list; tcache.list=this; }
  const uint64_t* ptr(unsigned k) { dieif(k>tcache.size(), "cache index %u out of bounds", k); return &array[k]; }
  uint64_t* wptr(unsigned k) { dieif(k>tcache.size(), "cache index %u out of bounds", k); return &array[k]; }
  uint64_t operator[](unsigned k) const { dieif(k>tcache.size(), "cache index %u out of bounds", k); return array[k]; }
};


/*
  The performance counter array matches the translation cache array.
  The basic block execution count matches the basic block header.
  Generic counters matches instructions.  The slots cooresponding to
  branch pointers can count different things like number of times,
  number of times branch prediction failed, etc.
*/

class hart_t : public hart_base_t {
  static volatile long global_time;
  long local_time;
  long _executed;
  void initialize();
  
public:
  Counters_t counters;
  cache_t* dc;
  
  hart_t(hart_base_t* from) :hart_base_t(from) { initialize(); }
  hart_t(int argc, const char* argv[], const char* envp[]) :hart_base_t(argc, argv, envp) { initialize(); }

  long executed() { return _executed; }
  static long total_count();
  void more_insn(long n) { _executed += n; }
  
  void addtime(long delta) { local_time+=delta; }
  long local_clock() { return local_time; }
  
  long system_clock() { return global_time; }
  void update_time();
  
  static hart_t* list() { return (hart_t*)hart_base_t::list(); }
  hart_t* next() { return (hart_t*)hart_base_t::next(); }

  void print();

  friend void simulator(hart_base_t* h, Header_t* bb);
};

void view_simulator(hart_base_t* h, long index);
void dumb_simulator(hart_base_t* h, long index);
void status_report();
