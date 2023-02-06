
#include "cache.h"


/*
  The performance counter array matches the translation cache array.
  The basic block execution count matches the basic block header.
  Generic counters matches instructions.  The slots cooresponding to
  branch pointers can count different things like number of times,
  number of times branch prediction failed, etc.
*/

class core_t : public hart_t {
  static volatile long global_time;
  long local_time;
  long _executed;
  long next_report;
  long* counter;		// performance counter array, if enabled
  void initialize();
public:
  cache_t* dc;
  cache_t* vc;
  core_t(core_t* from) :hart_t(from) { initialize(); }
  core_t(int argc, const char* argv[], const char* envp[], bool counters) :hart_t(argc, argv, envp, counters) { initialize(); }

  //  core_t* newcore() { return new core_t(this); }
  //  void proxy_syscall(long sysnum);
  long executed() { return _executed; }
  long more_insn(long n) { _executed+=n; return _executed; }
  static long total_count();
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  long system_clock() { return global_time; }
  long local_clock() { return local_time; }
  void update_time();
  void print();

  friend void simulator(hart_t* h, Header_t* bb);
};

void simulator(hart_t* h, Header_t* bb);
void status_report();
