/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

class hart_t : public mmu_t {
  class processor_t* spike_cpu;	// opaque pointer to Spike structure
  static volatile hart_t* cpu_list;	// for find() using thread id
  hart_t* link;				// list of hart_t
  int my_tid;				// my Linux thread number
  volatile long _executed;		// executed this thread
  volatile int clone_lock;	// 0=free, 1=locked
  friend int thread_interpreter(void* arg);
  friend int fork_interpreter(void* arg);
public:
  hart_t();
  virtual void proxy_syscall(long sysnum);
  void copy_state(hart_t* h);
  virtual hart_t* duplicate() { hart_t* h=new hart_t(); h->copy_state(this); return h; }
  void simulate(event_t* buffer, int last, int now) { }
  void proxy_ecall();
  
  static class hart_t* list() { return (class hart_t*)cpu_list; }
  class hart_t* next() { return link; }
  long executed() { return _executed; }
  long tid() { return my_tid; }
  void set_tid();
  static hart_t* find(int tid);
  bool single_step();
  void interpreter();
  
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
};

class hart_t* find_cpu(int tid);
