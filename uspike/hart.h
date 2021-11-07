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

#define PCTRACEBUFSZ  (1<<8)
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

class hart_t : public mmu_t {
  class processor_t* spike_cpu;	// opaque pointer to Spike structure
  static volatile hart_t* cpu_list;	// for find() using thread id
  hart_t* link;				// list of hart_t
  int my_tid;				// my Linux thread number
  volatile long _executed;		// executed this thread
  volatile int clone_lock;	// 0=free, 1=locked
  friend int thread_interpreter(void* arg);
  friend int fork_interpreter(void* arg);

  virtual long load_model( long a,  long pc) { return a;   }
  virtual long store_model(long a,  long pc) { return a;   }
  virtual void amo_model(  long a,  long pc) {             }
  virtual void custom(              long pc) {             }
public:
  hart_t();
  void copy_state(hart_t* from);
  virtual hart_t* newcore() { hart_t* h=new hart_t(); h->copy_state(this); return h; }
  virtual void proxy_syscall(long sysnum);
  void proxy_ecall();
  
  static class hart_t* list() { return (class hart_t*)cpu_list; }
  class hart_t* next() { return link; }
  long executed() { return _executed; }
  long tid() { return my_tid; }
  void set_tid();
  static hart_t* find(int tid);
  bool single_step();
  long interpreter(long& jpc); // returns number of instructions before jump/ecall
  virtual void run_thread();
  
  class processor_t* spike() { return spike_cpu; }
  class mmu_t* mmu() { return this; }
  long read_reg(int n);
  void write_reg(int n, long value);
  long* reg_file();
  void* freg_file();
  long read_pc();
  void write_pc(long value);
  long* ptr_pc();

  template<class T> bool cas(long pc);

#ifdef DEBUG
  Debug_t debug;
#endif
};

class hart_t* find_cpu(int tid);
