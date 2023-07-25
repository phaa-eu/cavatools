/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern "C" {
#include "softfloat.h"
#include "softfloat_types.h"
};

#ifdef SPIKE
#include "../spike/spike_link.h"
#include "../spike/processor.h"
#else
typedef int64_t		sreg_t;
typedef uint64_t	reg_t;
typedef float128_t	freg_t;
#endif






struct pctrace_t {
  uintptr_t pc;
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

#ifdef SPIKE

extern option<> conf_isa;
extern option<> conf_vec;

struct processor_state_t {
  processor_t spike_cpu;
  mmu_t spike_mmu;
  
#define xrf spike_cpu.get_state()->XPR
#define frf spike_cpu.get_state()->FPR
  
  //  fcsr_t fcsr;
  processor_state_t() :spike_cpu(conf_isa(), "mu", conf_vec(), 0, 0, false, stdout, &spike_mmu) {  }
  ~processor_state_t() { }
};

#else

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

class strand_t {
  class hart_base_t* hart_pointer;	// simulation object

  Tcache_t tcache;
  
  processor_state_t s;
#ifdef SPIKE
  const processor_t*& p = s.state;
  reg_t& pc = s.spike_cpu.get_state()->pc;
#else
  uintptr_t pc;
#endif
  
  static volatile strand_t* _list;	// for find() using thread id
  volatile strand_t* _next;		// list of strand_t
  int sid;				// strand index number
  static volatile int num_strands;	// cloned in process
  
  int tid;				// Linux thread number
  pthread_t ptnum;			// pthread handle
  
  Debug_t debug;

  void initialize(class hart_base_t* h);

  friend class hart_base_t;
  friend void controlled_by_gdb(const char* host_port, hart_base_t* cpu);
  friend void thread_interpreter(strand_t* me);
  friend void terminate_threads();
  friend int clone_thread(hart_base_t* h);

  friend void exit_func();
    
public:
  strand_t(class hart_base_t* h, int argc, const char* argv[], const char* envp[]);
  strand_t(class hart_base_t* h, strand_t* p);
  ~strand_t();

  void riscv_syscall();
  
  int interpreter();
  bool single_step(bool show_trace =false);
  void print_trace(uintptr_t pc, Insn_t* i, FILE* out =stderr);
  void debug_print() { debug.print(); }
  
  static strand_t* find(int tid);

  template<typename op>	uint64_t csr_func(uint64_t what, op f) {
    uint64_t old = get_csr(what);
    set_csr(what, f(old));
    return old;
  }

  template<class T> bool cas(const Insn_t* i, uintptr_t*& ap)
  {
    T* ptr = (T*)s.xrf[i->rs1()];
    T replace =  s.xrf[i->rs2()];
    T expect  =  s.xrf[i->rs3()];
    T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
    *ap++ = (long)ptr;
    return (oldval != expect);
  }

  template<typename op>	int32_t amo_int32(uintptr_t a, op f, uintptr_t*& ap) {
    int32_t lhs, *ptr = (int32_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (long)ptr;
    return lhs;
  }
  template<typename op>	int64_t amo_int64(uintptr_t a, op f, uintptr_t*& ap) {
    int64_t lhs, *ptr = (int64_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    *ap++ = (long)ptr;
    return lhs;
  }

  reg_t get_csr(int which, insn_t insn, bool write, bool peek =0);
  reg_t get_csr(int which) { return get_csr(which, insn_t(0), false, true); }
  void set_csr(int which, reg_t val);
};

class strand_t* find_cpu(int tid);
