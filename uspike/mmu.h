
class mmu_t {
  
#define load_func(type, prefix, xlate_flags)				\
  inline type##_t prefix##_##type(reg_t addr, bool require_alignment = false) { \
    return *(type##_t*)(addr); \
  }
#define store_func(type, prefix, xlate_flags)	     \
  inline void prefix##_##type(reg_t addr, type##_t val) {   \
    *(type##_t*)(addr) = val;				    \
  }
#define amo_func(type)	  \
  template<typename op>	type##_t amo_##type(reg_t addr, op f) { \
    auto lhs = load_##type(addr, true);				\
    store_##type(addr, f(lhs));					\
    return lhs;							\
  }
  
 public:
  load_func(uint8,  load, 0);
  load_func(uint16, load, 0);
  load_func(uint32, load, 0);
  load_func(uint64, load, 0);

  load_func(int8,  load, 0);
  load_func(int16, load, 0);
  load_func(int32, load, 0);
  load_func(int64, load, 0);

  store_func(uint8,  store, 0);
  store_func(uint16, store, 0);
  store_func(uint32, store, 0);
  store_func(uint64, store, 0);

  store_func(int8,  store, 0);
  store_func(int16, store, 0);
  store_func(int32, store, 0);
  store_func(int64, store, 0);

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

  void acquire_load_reservation(reg_t) { }
  bool check_load_reservation(reg_t, int) { return 1; }
  void yield_load_reservation() { }
  void flush_icache() { }
  void flush_tlb() { }
};
