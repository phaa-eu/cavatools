/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern "C" {
#include "softfloat.h"
#include "softfloat_types.h"
};

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

#if 0
class xreg_t {
  long v;
public:
  operator  int64_t() { return { v }; }
  operator uint64_t() { return { v }; }
  operator  int32_t() { return { v }; }
  operator uint32_t() { return { v }; }
  void operator=(long w) { v=w; }
};

class freg_t {
  union {
    double d;
    float f;
    uint64_t ul;
    uint32_t ui[2];
  } v;
public:
  operator float64_t() { return { v.d }; }
  operator float32_t() { return { v.f }; }
  void operator=(float64_t w) { v.d=w; }
  void operator=(float32_t w) { v.f=w; v.ui[1]=~0; }
};
#endif

typedef int64_t		sreg_t;
typedef uint64_t	reg_t;
typedef float128_t	freg_t;

#define xlen  64
#define sext32(x) ((sreg_t)(int32_t)(x))
#define zext32(x) ((reg_t)(uint32_t)(x))
#define sext_xlen(x) (((sreg_t)(x) << (64-xlen)) >> (64-xlen))
#define zext(x, pos) (((reg_t)(x) << (64-(pos))) >> (64-(pos)))
#define zext_xlen(x) zext(x, xlen)


#define defaultNaNF32UI 0x7FC00000
#define defaultNaNF64UI 0x7FF8000000000000L
#define isNaNF32UI( a ) (((~(a) & 0x7F800000) == 0) && ((a) & 0x007FFFFF))
#define isNaNF64UI( a ) (((~(a) & 0x7FF0000000000000L) == 0) && ((a) & 0x000FFFFFFFFFFFFFL))

inline int32_t f32_classify(float32_t v) { return 0; }
inline int64_t f64_classify(float64_t v) { return 0; }


/* Convenience wrappers to simplify softfloat code sequences */
#define isBoxedF32(r) (isBoxedF64(r) && ((uint32_t)((r.v[0] >> 32) + 1) == 0))
#define unboxF32(r) (isBoxedF32(r) ? (uint32_t)r.v[0] : defaultNaNF32UI)
#define isBoxedF64(r) ((r.v[1] + 1) == 0)
#define unboxF64(r) (isBoxedF64(r) ? r.v[0] : defaultNaNF64UI)
inline float32_t f32(uint32_t v) { return { v }; }
inline float64_t f64(uint64_t v) { return { v }; }
inline float32_t f32(freg_t r) { return f32(unboxF32(r)); }
inline float64_t f64(freg_t r) { return f64(unboxF64(r)); }
inline float128_t f128(freg_t r) { return r; }
inline freg_t freg(float32_t f) { return { ((uint64_t)-1 << 32) | f.v, (uint64_t)-1 }; }
inline freg_t freg(float64_t f) { return { f.v, (uint64_t)-1 }; }
inline freg_t freg(float128_t f) { return f; }
#define F32_SIGN ((uint32_t)1 << 31)
#define F64_SIGN ((uint64_t)1 << 63)
#define fsgnj32(a, b, n, x) \
  f32((f32(a).v & ~F32_SIGN) | ((((x) ? f32(a).v : (n) ? F32_SIGN : 0) ^ f32(b).v) & F32_SIGN))
#define fsgnj64(a, b, n, x) \
  f64((f64(a).v & ~F64_SIGN) | ((((x) ? f64(a).v : (n) ? F64_SIGN : 0) ^ f64(b).v) & F64_SIGN))



class hart_t {
  //  class processor_t* spike_cpu;	// opaque pointer to Spike structure
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
  hart_t(mmu_t* m, hart_t* p =0);
  virtual hart_t* newcore() { return new hart_t(new mmu_t, this); }
  //  virtual void proxy_syscall(long sysnum);
  void proxy_syscall(long sysnum);
  void proxy_ecall(long insns);
  
  static class hart_t* list() { return (class hart_t*)cpu_list; }
  class hart_t* next() { return link; }
  static int threads() { return num_threads; }
  int number() { return _number; }
  long executed() { return _executed; }
  void incr_count(long n);
  static long total_count() { return total_insns; }
  long tid() { return my_tid; }
  void set_tid();
  static hart_t* find(int tid);
  bool interpreter(long how_many);
  
  //  class processor_t* spike() { return spike_cpu; }
  class mmu_t* mmu() { return caveat_mmu; }
  
  long read_reg(int n) { return xrf[n]; }
  void write_reg(int n, long value) { xrf[n]=value; }
  long* reg_file() { return (long*)xrf; }
  long read_pc() { return pc; }
  void write_pc(long value) { pc=value; }
  long* ptr_pc() { return &pc; }

  long get_csr(int what, bool write);
  void set_csr(int what, long value);

  template<class T> bool cas(long pc);

#ifdef DEBUG
  Debug_t debug;
#endif
};

class hart_t* find_cpu(int tid);
