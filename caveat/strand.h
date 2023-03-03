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

#if 0
#define xlen  64
#define sext32(x) ((sreg_t)(int32_t)(x))
#define zext32(x) ((reg_t)(uint32_t)(x))
#define sext_xlen(x) (((sreg_t)(x) << (64-xlen)) >> (64-xlen))
#define zext(x, pos) (((reg_t)(x) << (64-(pos))) >> (64-(pos)))
#define zext_xlen(x) zext(x, xlen)
#endif






struct pctrace_t {
  long pc;
  reg_t val;
  Insn_t i;
};

#define PCTRACEBUFSZ  (1<<7)
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
  long pc;
  
  static volatile strand_t* _list;	// for find() using thread id
  volatile strand_t* _next;		// list of strand_t
  int sid;				// strand index number
  static volatile int num_strands;	// cloned in process
  
  int tid;				// Linux thread number
  pthread_t ptnum;			// pthread handle
  
  long addresses[1000];		// address list is one per strand
  Debug_t debug;

  Tcache_t tcache;		// translated instruction cache
  Tcache_t counters;		// counting array (1:1 with tcache)
  std::unordered_map<long, Header_t*> bbmap;

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
  void print_trace(long pc, Insn_t* i);
  void debug_print() { debug.print(); }
  
  static strand_t* find(int tid);

  long get_csr(int what);
  void set_csr(int what, long value);

  template<typename op>	uint64_t csr_func(uint64_t what, op f) {
    uint64_t old = get_csr(what);
    set_csr(what, f(old));
    return old;
  }

  template<class T> bool cas(Insn_t* i, long*& ap)
  {
    T* ptr = (T*)s.xrf[i->rs1()];
    T replace =  s.xrf[i->rs2()];
    T expect  =  s.xrf[i->rs3()];
    T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
    *ap++ = (long)ptr;
    return (oldval != expect);
  }

  template<typename op>	int32_t amo_int32(long a, op f, long*& ap) {
    int32_t lhs, *ptr = (int32_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (long)ptr;
    return lhs;
  }
  template<typename op>	int64_t amo_int64(long a, op f, long*& ap) {
    int64_t lhs, *ptr = (int64_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (long)ptr;
    return lhs;
  }
};

class strand_t* find_cpu(int tid);
