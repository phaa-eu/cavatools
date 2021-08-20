#include <atomic>

class mmu_t {
  static long reserve_addr;
  
#define load_func(type, prefix, xlate_flags)				\
  inline type##_t prefix##_##type(/*reg_t*/long addr, bool require_alignment = false) { \
    return *(type##_t*)(addr); \
  }
#define store_func(type, prefix, xlate_flags)	     \
  inline void prefix##_##type(/*reg_t*/long addr, type##_t val) {   \
    *(type##_t*)(addr) = val;				    \
  }
  
#define amo_func(type)						\
  template<typename op>	type##_t amo_##type(/*reg_t*/long addr, op f) { \
    type##_t lhs, *ptr = (type##_t*)addr;			\
    do lhs = *ptr;						\
    while (!__sync_bool_compare_and_swap(ptr, lhs, f(lhs)));	\
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

  mmu_t();
  void acquire_load_reservation(long a);
  void yield_load_reservation();
  bool check_load_reservation(long a, size_t size);
  void flush_icache() { }
  void flush_tlb() { }
};

class Mutex_t {
public:
  Mutex_t() : atom_(0) {}
  void lock();
  void unlock();
private:
  // 0 means unlocked
  // 1 means locked, no waiters
  // 2 means locked, there are waiters in lock()
  std::atomic<int> atom_;
};
