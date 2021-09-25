/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

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

class hart_t {
  class processor_t* spike_cpu;	// opaque pointer to Spike structure
  class mmu_t* caveat_mmu;	// opaque pointer to our MMU
  static volatile hart_t* cpu_list;	// for find() using thread id
  hart_t* link;				// list of hart_t
  int my_tid;				// my Linux thread number
  static volatile int num_threads;	// allocated
  int _number;				// index of this hart
  static volatile long total_insns;	// instructions executed all threads
  long _executed;			// executed this thread
  volatile int clone_lock;	// 0=free, 1=locked
  friend int thread_interpreter(void* arg);
public:
  hart_t(mmu_t* m);
  hart_t(hart_t* p, mmu_t* m);
  virtual hart_t* newcore() { return new hart_t(this, new mmu_t); }
  virtual void proxy_syscall(long sysnum);
  void proxy_ecall(long insns);
  
  static class hart_t* list() { return (class hart_t*)cpu_list; }
  class hart_t* next() { return link; }
  static int threads() { return num_threads; }
  int number() { return _number; }
  long executed() { return _executed; }
  void incr_count(long n);
  static volatile long total_count() { return total_insns; }
  long tid() { return my_tid; }
  void set_tid();
  static hart_t* find(int tid);
  bool interpreter(long how_many);
  virtual void run_thread();
  static void status_report();
  
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

class hart_t* find_cpu(int tid);
