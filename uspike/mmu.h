/* Some Spike instruction semantics files include ../mmu.h
 */

#ifndef MMU_H
#define MMU_H

class mmu_t {
  long va;
  
#define load_func(type, prefix, xlate_flags)				\
  inline type##_t prefix##_##type(long addr, bool ra = false) {		\
    return *(type##_t*)(va=addr);					\
  }
#define store_func(type, prefix, xlate_flags)		    \
  inline void prefix##_##type(long addr, type##_t val) {    \
    *(type##_t*)(va=addr) = val;				    \
  }
  
#define amo_func(type)						\
  template<typename op>	type##_t amo_##type(long addr, op f) {	\
    type##_t lhs, *ptr = (type##_t*)(va=addr);			\
    do lhs = *ptr;						\
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));	\
    return lhs;							\
  }
  
 public:
  mmu_t() { va=0; }
  long VA() { return va; }
  
  load_func(uint8,  load, 0);
  load_func(uint16, load, 0);
  load_func(uint32, load, 0);
  load_func(uint64, load, 0);

  load_func(int8,  load, 0);
  load_func(int16, load, 0);
  load_func(int32, load, 0);
  load_func(int64, load, 0);

  //  float  load_float32(long addr) { union { float  f; int  i; } x; x.i=load_int32(addr); return x.f; }
  //  double load_float64(long addr) { union { double d; long l; } x; x.l=load_int64(addr); return x.d; }
  float  load_fp32(long addr) { return *( float*)addr; }
  double load_fp64(long addr) { return *(double*)addr; }

  store_func(uint8,  store, 0);
  store_func(uint16, store, 0);
  store_func(uint32, store, 0);
  store_func(uint64, store, 0);

  store_func(int8,  store, 0);
  store_func(int16, store, 0);
  store_func(int32, store, 0);
  store_func(int64, store, 0);
  
  void store_fp32(long addr, float  v) { union { float  f; int  i; } x; x.f=v; store_int32(addr, x.i); }
  void store_fp64(long addr, double v) { union { double d; long l; } x; x.d=v; store_int64(addr, x.l); }

  load_func(uint8,  guest_load, 0);
  load_func(uint16, guest_load, 0);
  load_func(uint32, guest_load, 0);
  load_func(uint64, guest_load, 0);

  load_func(int8,  guest_load, 0);
  load_func(int16, guest_load, 0);
  load_func(int32, guest_load, 0);
  load_func(int64, guest_load, 0);
  
  load_func(uint16, guest_load_x, 0);
  load_func(uint32, guest_load_x, 0);

  store_func(uint8,  guest_store, 0);
  store_func(uint16, guest_store, 0);
  store_func(uint32, guest_store, 0);
  store_func(uint64, guest_store, 0);

  store_func(int8,  guest_store, 0);
  store_func(int16, guest_store, 0);
  store_func(int32, guest_store, 0);
  store_func(int64, guest_store, 0);

  amo_func(uint32);
  amo_func(uint64);

  void acquire_load_reservation(long a) { }
  void yield_load_reservation() { }
  bool check_load_reservation(long a, long size) { return true; }
  void flush_icache() { }
  void flush_tlb() { }
};

#endif
