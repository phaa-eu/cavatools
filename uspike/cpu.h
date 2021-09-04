#include <stdint.h>

#include "mmu.h"

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
  class mmu_t caveat_mmu;	// opaque pointer to our MMU
  static cpu_t* cpu_list;	// for find() using thread id
  cpu_t* link;			// list of cpu_t
  long insn_count;		// instructions executed this thread
  static long total_insns;	// instructions executed all threads
  static int num_threads;
  int my_tid;			// my Linux thread number
public:
  cpu_t();
  cpu_t(cpu_t* p);
  cpu_t(int argc, const char* argv[], const char* envp[]);
    
  static class cpu_t* list() { return cpu_list; }
  class cpu_t* next() { return link; }
  static int threads() { return num_threads; }
  long count() { return insn_count; }
  void incr_count(long n);
  static long total_count() { return total_insns; }
  long tid() { return my_tid; }
  static cpu_t* find(int tid);
  
  class processor_t* spike() { return spike_cpu; }
  class mmu_t* mmu() { return &caveat_mmu; }
  long read_reg(int n);
  void write_reg(int n, long value);
  long* reg_file();
  long read_pc();
  void write_pc(long value);
  long* ptr_pc();
  bool proxy_ecall(long cycles);
  
#ifdef DEBUG
  Debug_t debug;
#endif
};

class cpu_t* find_cpu(int tid);
