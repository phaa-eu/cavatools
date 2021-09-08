/* Some Spike instruction semantics files include ../mmu.h
 */

#ifndef MMU_H
#define MMU_H

class mmu_t {
  virtual long load_model( long a, int b) { return a; }
  virtual long store_model(long a, int b) { return a; }
  virtual void amo_model(  long a, int b) { }
  
#define load_func(type, prefix, len)				\
  inline type##_t prefix##_##type(long addr, bool boo =false) {	\
  return *(type##_t*)load_model(addr, len);			\
  }
#define store_func(type, prefix, len)				\
  inline void prefix##_##type(long addr, type##_t val) {	\
    *(type##_t*)store_model(addr, len) = val;			\
  }
  
#define amo_func(type, len)					\
  template<typename op>	type##_t amo_##type(long addr, op f) {	\
    type##_t lhs, *ptr = (type##_t*)addr;			\
    do lhs = *ptr;						\
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));	\
    amo_model(addr, len);					\
    return lhs;							\
  }
  
 public:
  mmu_t() { }
  
  load_func(uint8,  load, 1);
  load_func(uint16, load, 2);
  load_func(uint32, load, 4);
  load_func(uint64, load, 8);

  load_func(int8,  load, 1);
  load_func(int16, load, 2);
  load_func(int32, load, 4);
  load_func(int64, load, 8);

  float  load_fp32(long addr) { return ( float)load_int32(addr); }
  double load_fp64(long addr) { return (double)load_int64(addr); }

  store_func(uint8,  store, 1);
  store_func(uint16, store, 2);
  store_func(uint32, store, 4);
  store_func(uint64, store, 8);

  store_func(int8,  store, 1);
  store_func(int16, store, 2);
  store_func(int32, store, 4);
  store_func(int64, store, 8);
  
  void store_fp32(long addr, float  v) { union { float  f; int  i; } x; x.f=v; store_int32(addr, x.i); }
  void store_fp64(long addr, double v) { union { double d; long l; } x; x.d=v; store_int64(addr, x.l); }

  load_func(uint8,  guest_load, 1);
  load_func(uint16, guest_load, 2);
  load_func(uint32, guest_load, 4);
  load_func(uint64, guest_load, 8);

  load_func(int8,  guest_load, 1);
  load_func(int16, guest_load, 2);
  load_func(int32, guest_load, 4);
  load_func(int64, guest_load, 8);
  
  load_func(uint16, guest_load_x, 2);
  load_func(uint32, guest_load_x, 4);

  store_func(uint8,  guest_store, 1);
  store_func(uint16, guest_store, 2);
  store_func(uint32, guest_store, 4);
  store_func(uint64, guest_store, 8);

  store_func(int8,  guest_store, 1);
  store_func(int16, guest_store, 2);
  store_func(int32, guest_store, 4);
  store_func(int64, guest_store, 8);

  amo_func(uint32, 4);
  amo_func(uint64, 8);

  void acquire_load_reservation(long a) { }
  void yield_load_reservation() { }
  bool check_load_reservation(long a, long size) { return true; }
  void flush_icache() { }
  void flush_tlb() { }
};

#endif
