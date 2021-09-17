#include <stdint.h>

#ifdef DEBUG
struct pctrace_t {
  long count;
  long pc;
  long val;
  int8_t rn;
};

#define PCTRACEBUFSZ  (1<<7)
struct Debug_t {
  pctrace_t trace[PCTRACEBUFSZ];
  int cursor;
  Debug_t() { cursor=0; }
  pctrace_t get();
  void insert(pctrace_t pt);
  void insert(long c, long pc);
  void addval(int rn, long val);
  void print();
};
#endif

class cpu_t {
  class processor_t* spike_cpu;	// opaque pointer to Spike structure
  class mmu_t* caveat_mmu;	// opaque pointer to our MMU
  static volatile cpu_t* cpu_list;	// for find() using thread id
  cpu_t* link;				// list of cpu_t
  int my_tid;				// my Linux thread number
  static volatile int num_threads;	// allocated
  int _number;				// index of this cpu
  static volatile long total_insns;	// instructions executed all threads
  long insn_count;			// executed this thread
  volatile int clone_lock;	// 0=free, 1=locked
  friend int thread_interpreter(void* arg);
public:
  cpu_t();
  cpu_t(cpu_t* p, mmu_t* m);
  cpu_t(int argc, const char* argv[], const char* envp[], mmu_t* m);
  virtual cpu_t* newcore() { return new cpu_t(this, new mmu_t); }
  virtual void proxy_syscall(long sysnum);
  void proxy_ecall(long insns);
  
  static class cpu_t* list() { return (class cpu_t*)cpu_list; }
  class cpu_t* next() { return link; }
  static int threads() { return num_threads; }
  int number() { return _number; }
  long count() { return insn_count; }
  void incr_count(long n);
  static long total_count() { return total_insns; }
  long tid() { return my_tid; }
  void set_tid();
  static cpu_t* find(int tid);
  bool single_step();
  bool run_epoch(long how_many);
  
  class processor_t* spike() { return spike_cpu; }
  class mmu_t* mmu() { return caveat_mmu; }
  long read_reg(int n);
  void write_reg(int n, long value);
  long* reg_file();
  long read_pc();
  void write_pc(long value);
  long* ptr_pc();

  template<class T> bool cas(long pc);

#ifdef DEBUG
  Debug_t debug;
#endif
};

class cpu_t* find_cpu(int tid);
