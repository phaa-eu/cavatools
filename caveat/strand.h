/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern "C" {
#include "softfloat/softfloat.h"
#include "softfloat/softfloat_types.h"
};
//#include "mmu.h"

typedef int64_t		sreg_t;
typedef uint64_t	reg_t;
typedef float128_t	freg_t;

#define xlen  64
#define sext32(x) ((sreg_t)(int32_t)(x))
#define zext32(x) ((reg_t)(uint32_t)(x))
#define sext_xlen(x) (((sreg_t)(x) << (64-xlen)) >> (64-xlen))
#define zext(x, pos) (((reg_t)(x) << (64-(pos))) >> (64-(pos)))
#define zext_xlen(x) zext(x, xlen)







#ifdef DEBUG
struct pctrace_t {
  long count;
  long pc;
  Insn_t* i;
  reg_t val;
};

#define PCTRACEBUFSZ  (1<<7)
struct Debug_t {
  pctrace_t trace[PCTRACEBUFSZ];
  int cursor;
  Debug_t() { cursor=0; }
  pctrace_t get();
  void insert(pctrace_t pt);
  void insert(long c, long pc, Insn_t* i);
  void addval(reg_t val);
  void print();
};
#endif


class strand_t {
  class hart_t* hart;			// simulation object
  long* addresses;			// list of load/store addr
  reg_t  xrf[32];
  freg_t frf[32];
  long pc;
  union {
    struct {
      unsigned flags : 5;
      unsigned rm :3;
    } f;
    uint32_t ui;
  } fcsr;
  
private:  
  long _executed;			// executed this thread
  long next_report;
  volatile int clone_lock;	// 0=free, 1=locked
  friend int thread_interpreter(void* arg);
public:
  strand_t(strand_t* p);
  friend hart_t::hart_t(hart_t* from);
  friend hart_t::hart_t(int argc, const char* argv[], const char* envp[]);
  
  //  virtual strand_t* newcore() { abort(); }
  //  virtual void proxy_syscall(long sysnum);
  void proxy_syscall(long sysnum);
  void proxy_ecall();
  
  long executed() { return _executed; }
  static long total_count();
  void interpreter(simfunc_t f, statfunc_t s);
  void single_step();
  void print_trace(long pc, Insn_t* i);

  long get_csr(int what);
  void set_csr(int what, long value);

  template<typename op>	uint64_t csr_func(uint64_t what, op f) {
    uint64_t old = get_csr(what);
    set_csr(what, f(old));
    return old;
  }

  template<class T> bool cas(long pc)
  {
    Insn_t i = code.at(pc);
    T* ptr = (T*)xrf[i.rs1()];
    T expect  =  xrf[i.rs2()];
    T replace =  xrf[i.rs3()];
    T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
    xrf[code.at(pc+4).rs1()] = oldval;
    if (oldval == expect)  xrf[i.rd()] = 0;;	/* sc was successful */
    return oldval == expect;
  }

  template<typename op>	uint32_t amo_uint32(long a, op f) {
    uint32_t lhs, *ptr = (uint32_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    //    amo_model(a, pc);
    return lhs;
  }
  template<typename op>	uint64_t amo_uint64(long a, op f) {
    uint64_t lhs, *ptr = (uint64_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    //    amo_model(a, pc);
    return lhs;
  }

  template<typename T> inline T* Load(long a) { return (T*)a; }
  template<typename T> inline void Store(long a, T v) { *(T*)a = v; }

  void acquire_reservation(long a) { }
  void yield_reservation() { }
  bool check_reservation(long a, long size) { return true; }
  void flush_icache() { }
  void flush_tlb() { }

#ifdef DEBUG
  Debug_t debug;
#endif
};

class strand_t* find_cpu(int tid);
