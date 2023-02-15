
#include "cache.h"


/*
  The performance counter array matches the translation cache array.
  The basic block execution count matches the basic block header.
  Generic counters matches instructions.  The slots cooresponding to
  branch pointers can count different things like number of times,
  number of times branch prediction failed, etc.
*/

extern option<long> conf_report;

class hart_t : public hart_base_t {
  static volatile long global_time;
  long local_time;
  long _executed;
  long next_report;
  long* counter;		// performance counter array, if enabled
  void initialize();
public:
  cache_t* dc;
  cache_t* vc;
  hart_t(hart_t* from) :hart_base_t(from) { initialize(); }
  hart_t(int argc, const char* argv[], const char* envp[], bool counters) :hart_base_t(argc, argv, envp, counters) { initialize(); }

  //  hart_t* newcore() { return new hart_t(this); }
  //  void proxy_syscall(long sysnum);
  long executed() { return _executed; }
  static long total_count();
  bool more_insn(long n) { bool s=(_executed+=n)>next_report; if (s) next_report+=conf_report; return s; }
  
  void advance(long delta) { local_time+=delta; }
  long local_clock() { return local_time; }
  
  long system_clock() { return global_time; }
  void update_time();
  
  static hart_t* list() { return (hart_t*)hart_base_t::list(); }
  hart_t* next() { return (hart_t*)hart_base_t::next(); }

  void print();

  friend void simulator(hart_base_t* h, Header_t* bb);
};

void view_simulator(hart_base_t* h, Header_t* bb);
void dumb_simulator(hart_base_t* h, Header_t* bb);
void status_report();
