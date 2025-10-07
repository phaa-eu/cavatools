/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern "C" {
#include "softfloat.h"
#include "softfloat_types.h"
};

extern option<> conf_gdb;
extern option<bool> conf_show;


struct pctrace_t {
  uintptr_t pc;
  long val;
  Insn_t i;
};

#define PCTRACEBUFSZ  32
struct Debug_t {
  pctrace_t trace[PCTRACEBUFSZ];
  int cursor;
  Debug_t() { cursor=0; }
  pctrace_t get();
#ifdef DEBUG
  void insert(pctrace_t pt);
  void insert(long pc, Insn_t i);
  void addval(long val);
  void print();
#else
  void insert(pctrace_t pt) { }
  void insert(long pc, Insn_t i) { }
  void addval(long val) { }
  void print() { }
#endif
};





#ifdef SPIKE

#include "../spike/spike_link.h"
#include "../spike/processor.h"

#define spike_isa "rv64imafdcv"
#define spike_vec "vlen:128,elen:64,slen:128"



struct processor_state_t {
  processor_t spike_cpu;
  mmu_t spike_mmu;
  processor_state_t() :spike_cpu(spike_isa, "mu", spike_vec, 0, 0, false, stdout, &spike_mmu) {  }
  ~processor_state_t() { }
};
  
#define xrf spike_cpu.get_state()->XPR
#define frf spike_cpu.get_state()->FPR

#else

typedef int64_t		sreg_t;
typedef uint64_t	reg_t;
typedef float128_t	freg_t;

/*
  Processor status register
*/

struct processor_state_t {
  reg_t  xrf[32];
  freg_t frf[32];

  unsigned fflags;
  unsigned frm;
};


struct insn_t {
  uint64_t bits;
  insn_t(uint64_t x) { bits=x; }
};

#define READ_REG(n)   s.xrf[n]
#define READ_FREG(n)  s.frf[(n)-FPREG]

#define WRITE_REG(n, v)   s.xrf[n] = (v)
#define WRITE_FREG(n, v)  s.frf[(n)-FPREG] = (v)

#define CSR_FFLAGS 0x1
#define CSR_FRM 0x2
#define CSR_FCSR 0x3

#define FP_RD_NE  0
#define FP_RD_0   1
#define FP_RD_DN  2
#define FP_RD_UP  3
#define FP_RD_NMM 4

#define FSR_RD_SHIFT 5
#define FSR_RD   (0x7 << FSR_RD_SHIFT)

#define FPEXC_NX 0x01
#define FPEXC_UF 0x02
#define FPEXC_OF 0x04
#define FPEXC_DZ 0x08
#define FPEXC_NV 0x10

#define FSR_AEXC_SHIFT 0
#define FSR_NVA  (FPEXC_NV << FSR_AEXC_SHIFT)
#define FSR_OFA  (FPEXC_OF << FSR_AEXC_SHIFT)
#define FSR_UFA  (FPEXC_UF << FSR_AEXC_SHIFT)
#define FSR_DZA  (FPEXC_DZ << FSR_AEXC_SHIFT)
#define FSR_NXA  (FPEXC_NX << FSR_AEXC_SHIFT)
#define FSR_AEXC (FSR_NVA | FSR_OFA | FSR_UFA | FSR_DZA | FSR_NXA)

#endif



class hart_t {
  static int _num_harts;	// how many have been cloned
  static hart_t* _list;		// for find() using thread id
  hart_t* _next;		// list of hart_t
  int sid;			// strand index number
  int _tid;			// Linux thread number
  pthread_t ptnum;		// pthread handle
  
  void initialize();		// used by constructor functions

  friend void controlled_by_gdb(const char* host_port, hart_t* cpu);
  friend void thread_interpreter(hart_t* me);
  friend void terminate_threads();
  friend int clone_thread(hart_t* child);
  friend void exit_func();
    
public:
  processor_state_t s;
  long _executed;		// number of instructions
#ifdef SPIKE
  processor_t* p = &s.spike_cpu;
  reg_t& pc = s.spike_cpu.get_state()->pc;
#else
  uintptr_t pc;
#endif



  
  Tcache_t tcache;
  Debug_t debug;
  
  simfunc_t simulator;		// function pointer for simulation
  clonefunc_t clone;		// function pointer just for clone system call
  interpreterfunc_t interpreter;// function pointer for interpreter
  syscallfunc_t riscv_syscall;	// function pointer for system calls
  
  hart_t(int argc, const char* argv[], const char* envp[]);
  hart_t(hart_t* p);
  ~hart_t();

  Header_t* find_bb(uintptr_t pc);
  void default_interpreter();
  bool single_step();
  void print(uintptr_t pc, Insn_t* i, FILE* out =stderr);
  long executed() { return _executed; }
  void count_insn(int n =1) { _executed += n; }
  long flushed() { return tcache.flushed(); }
  void debug_print() { debug.print(); }

  static hart_t* list() { return _list; }
  hart_t* next() { return _next; }
  int tid() { return _tid; }
  static hart_t* find(int tid); // hart given Linux thread ID
  static int num_harts() { return _num_harts; }

  reg_t get_csr(int which, insn_t insn, bool write, bool peek =0);
  reg_t get_csr(int which) { return get_csr(which, insn_t(0), false, true); }
  void set_csr(int which, reg_t val);

  template<typename op>	uint64_t csr_func(uint64_t what, op f) {
    uint64_t old = get_csr(what);
    set_csr(what, f(old));
    return old;
  }
  template<class T> bool cas(long r1, T replace, T expect, uintptr_t*& ap)
  {
    T* ptr = (T*)r1;
    T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
    *ap++ = (uintptr_t)ptr;
    return (oldval != expect);
  }
  template<typename op>	int32_t amo_int32(uintptr_t a, op f, uintptr_t*& ap) {
    int32_t lhs, *ptr = (int32_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (uintptr_t)ptr;
    return lhs;
  }
  template<typename op>	int64_t amo_int64(uintptr_t a, op f, uintptr_t*& ap) {
    int64_t lhs, *ptr = (int64_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (uintptr_t)ptr;
    return lhs;
  }
};


void default_riscv_syscall(hart_t* h);
long proxy_syscall(long rvnum, long a0, long a1, long a2, long a3, long a4, long a5, hart_t* me);
