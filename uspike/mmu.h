/* Some Spike instruction semantics files include ../mmu.h
 */

#ifndef MMU_H
#define MMU_H

class mmu_t {
  virtual long load_model( long a, long pc) { return a; }
  virtual long store_model(long a, long pc) { return a; }
  virtual void amo_model(  long a, long pc) { }
  
 public:
  mmu_t() { }
  virtual void insn_model(long pc) { }
  virtual long jump_model(long npc, long pc) { return npc; }

  uint8_t  load_uint8( long a, long pc) { return *(uint8_t* )load_model(a, pc); }
  uint16_t load_uint16(long a, long pc) { return *(uint16_t*)load_model(a, pc); }
  uint32_t load_uint32(long a, long pc) { return *(uint32_t*)load_model(a, pc); }
  uint64_t load_uint64(long a, long pc) { return *(uint64_t*)load_model(a, pc); }

  int8_t  load_int8( long a, long pc) { return *(int8_t* )load_model(a, pc); }
  int16_t load_int16(long a, long pc) { return *(int16_t*)load_model(a, pc); }
  int32_t load_int32(long a, long pc) { return *(int32_t*)load_model(a, pc); }
  int64_t load_int64(long a, long pc) { return *(int64_t*)load_model(a, pc); }

  float  load_fp32(long a, long pc) { return ( float)load_int32(a, pc); }
  double load_fp64(long a, long pc) { return (double)load_int64(a, pc); }
  
  void store_uint8( long a, long pc, uint8_t  v) { *(uint8_t* )store_model(a, pc)=v; }
  void store_uint16(long a, long pc, uint16_t v) { *(uint16_t*)store_model(a, pc)=v; }
  void store_uint32(long a, long pc, uint32_t v) { *(uint32_t*)store_model(a, pc)=v; }
  void store_uint64(long a, long pc, uint64_t v) { *(uint64_t*)store_model(a, pc)=v; }
  
  void store_int8( long a, long pc, int8_t  v) { *(int8_t* )store_model(a, pc)=v; }
  void store_int16(long a, long pc, int16_t v) { *(int16_t*)store_model(a, pc)=v; }
  void store_int32(long a, long pc, int32_t v) { *(int32_t*)store_model(a, pc)=v; }
  void store_int64(long a, long pc, int64_t v) { *(int64_t*)store_model(a, pc)=v; }
  
  void store_fp32(long a, long pc, float  v) { union { float  f; int  i; } x; x.f=v; store_int32(a, pc, x.i); }
  void store_fp64(long a, long pc, double v) { union { double d; long l; } x; x.d=v; store_int64(a, pc, x.l); }

  template<typename op>	uint32_t amo_uint32(long a, long pc, op f) {
    uint32_t lhs, *ptr = (uint32_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    amo_model(a, pc);
    return lhs;
  }
  template<typename op>	uint64_t amo_uint64(long a, long pc, op f) {
    uint64_t lhs, *ptr = (uint64_t*)a;
    do lhs = *ptr;
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));
    amo_model(a, pc);
    return lhs;
  }

  void acquire_load_reservation(long a) { }
  void yield_load_reservation() { }
  bool check_load_reservation(long a, long size) { return true; }
  void flush_icache() { }
  void flush_tlb() { }
};

#define load_uint8( a)  load_uint8( a, pc)
#define load_uint16(a)  load_uint16(a, pc)
#define load_uint32(a)  load_uint32(a, pc)
#define load_uint64(a)  load_uint64(a, pc)

#define load_int8( a)  load_int8( a, pc)
#define load_int16(a)  load_int16(a, pc)
#define load_int32(a, ...)  load_int32(a, pc)
#define load_int64(a, ...)  load_int64(a, pc)

#define load_fp32(a)  load_fp32(a, pc)
#define load_fp64(a)  load_fp64(a, pc)

#define store_uint8( a, v)  store_uint8( a, pc, v)
#define store_uint16(a, v)  store_uint16(a, pc, v)
#define store_uint32(a, v)  store_uint32(a, pc, v)
#define store_uint64(a, v)  store_uint64(a, pc, v)

#define store_int8( a, v)  store_int8( a, pc, v)
#define store_int16(a, v)  store_int16(a, pc, v)
#define store_int32(a, v)  store_int32(a, pc, v)
#define store_int64(a, v)  store_int64(a, pc, v)

#define store_fp32(a, v)  store_fp32(a, pc, v)
#define store_fp64(a, v)  store_fp64(a, pc, v)

#define amo_uint32(a, f)  amo_uint32(a, pc, f)
#define amo_uint64(a, f)  amo_uint64(a, pc, f)

#endif
