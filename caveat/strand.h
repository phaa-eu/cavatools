/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern "C" {
#include "softfloat/softfloat.h"
#include "softfloat/softfloat_types.h"
};
#include <unordered_map>

typedef int64_t		sreg_t;
typedef uint64_t	reg_t;
typedef float128_t	freg_t;






struct pctrace_t {
  Addr_t pc;
  reg_t val;
  Insn_t i;
};

//#define PCTRACEBUFSZ  (1<<7)
#define PCTRACEBUFSZ  32
struct Debug_t {
  pctrace_t trace[PCTRACEBUFSZ];
  int cursor;
  Debug_t() { cursor=0; }
  pctrace_t get();
#ifdef DEBUG
  void insert(pctrace_t pt);
  void insert(long pc, Insn_t i);
  void addval(reg_t val);
  void print();
#else
  void insert(pctrace_t pt) { }
  void insert(long pc, Insn_t i) { }
  void addval(reg_t val) { }
  void print() { }
#endif
};

struct processor_state_t {
  reg_t  xrf[32];
  freg_t frf[32];
  
  union {
    struct {
      unsigned flags : 5;
      unsigned rm :3;
    } f;
    uint32_t ui;
  } fcsr;
};

class strand_t {
  class hart_base_t* hart_pointer;	// simulation object
  processor_state_t s;
  Addr_t pc;
  
  static volatile strand_t* _list;	// for find() using thread id
  volatile strand_t* _next;		// list of strand_t
  int sid;				// strand index number
  static volatile int num_strands;	// cloned in process
  
  int tid;				// Linux thread number
  pthread_t ptnum;			// pthread handle
  
  Addr_t addresses[1000];	// address list is one per strand
  Debug_t debug;

  std::unordered_map<long, const Header_t*> bbmap;

  void initialize(class hart_base_t* h);

  friend class hart_base_t;
  friend void controlled_by_gdb(const char* host_port, hart_base_t* cpu);
  //  friend void* thread_interpreter(void* arg);
  friend void thread_interpreter(strand_t* me);
  friend int clone_thread(hart_base_t* h);
  friend void terminate_threads();

  friend void exit_func();
    
public:
  strand_t(class hart_base_t* h, int argc, const char* argv[], const char* envp[]);
  strand_t(class hart_base_t* h, strand_t* p);

  void riscv_syscall();
  
  int interpreter();
  bool single_step(bool show_trace =false);
  void print_trace(Addr_t pc, Insn_t* i, FILE* out =stderr);
  void debug_print() { debug.print(); }
  
  static strand_t* find(int tid);

  long get_csr(int what);
  void set_csr(int what, long value);

  template<typename op>	uint64_t csr_func(uint64_t what, op f) {
    uint64_t old = get_csr(what);
    set_csr(what, f(old));
    return old;
  }

  template<class T> bool cas(const Insn_t* i, Addr_t*& ap)
  {
    T* ptr = (T*)s.xrf[i->rs1()];
    T replace =  s.xrf[i->rs2()];
    T expect  =  s.xrf[i->rs3()];
    T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
    *ap++ = (long)ptr;
    return (oldval != expect);
  }

  template<typename op>	int32_t amo_int32(Addr_t a, op f, Addr_t*& ap) {
    int32_t lhs, *ptr = (int32_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (long)ptr;
    return lhs;
  }
  template<typename op>	int64_t amo_int64(Addr_t a, op f, Addr_t*& ap) {
    int64_t lhs, *ptr = (int64_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (long)ptr;
    return lhs;
  }
};

class strand_t* find_cpu(int tid);
