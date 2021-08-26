#include <stdint.h>
//#define DEBUG

#ifdef DEBUG
struct pctrace_t {
  long count;
  long pc;
  long val;
  int8_t rn;
};

#define PCTRACEBUFSZ  (1<<5)
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

#include "opcodes.h"

class alignas(8) Insn_t {
  Opcode_t op_code;
  int8_t op_rd;		// note unsigned byte
  int8_t op_rs1;	// so NOREG==-1
  union {
    struct {
      int16_t imm;
      int8_t rs2;
      int8_t rs3;
    } op;
    int32_t op_longimm;
  };
  
public:
  Insn_t() { *((int64_t*)this) = -1; } // all registers become NOREG
  Insn_t(Opcode_t code)                { *((int64_t*)this)=-1; op_code=code; }
  Insn_t(Opcode_t code, int8_t rd, int16_t imm)     :Insn_t(code)  { op_rd=rd; op.imm =imm<<1|1; }

  long opcode() { return op_code; }
  int rd()  { return op_rd; }
  int rs1() { return op_rs1; }
  int rs2() { return op.rs2; }
  int rs3() { return op.rs3; }
  long immed() { return (op.imm&0x1) ? op.imm>>1 : op_longimm; }

  bool compressed() { return op_code <= Last_Compressed_Opcode; }
  bool longimmed() { return (op.imm & 0x1) == 0; }
  friend Insn_t reg1insn( Opcode_t code, int8_t rd, int8_t rs1);
  friend Insn_t reg2insn( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2);
  friend Insn_t reg3insn (Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3);
  friend Insn_t reg1imm(Opcode_t code, int8_t rd, int8_t rs1, int16_t imm);
  friend Insn_t reg2imm(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);
  friend Insn_t longimm(Opcode_t code, int8_t rd, int32_t longimmed);
};
static_assert(sizeof(Insn_t) == 8);

Insn_t reg1insn( Opcode_t code, int8_t rd, int8_t rs1);
Insn_t reg2insn( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2);
Insn_t reg3insn(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3);
Insn_t reg1imm( Opcode_t code, int8_t rd, int8_t rs1, int16_t imm);
Insn_t reg2imm( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);
Insn_t longimm( Opcode_t code, int8_t rd, int32_t longimmed);

#define GPREG	0
#define FPREG	GPREG+32
#define VPREG	FPREG+32
#define VMREG	VPREG+32
#define NOREG	-1

Insn_t decoder(int b, long pc);	// given bitpattern image of in struction


class cpu_t {
  class processor_t* spike_cpu;	// opaque pointer to Spike structure
  static cpu_t* cpu_list;	// for find() using thread id
  cpu_t* link;			// list of cpu_t
  int my_tid;			// my Linux thread number
public:
  static long reserve_addr;	// single lock for all cpu's
  long insn_count;
  cpu_t(processor_t* p);
  static class cpu_t* list() { return cpu_list; }
  class cpu_t* next() { return link; }
  class processor_t* spike() { return spike_cpu; }
  long tid() { return my_tid; }
  static cpu_t* find(int tid);
#ifdef DEBUG
  Debug_t debug;
#endif
  
#define load_func(type, prefix, xlate_flags)				\
  inline type##_t prefix##_##type(long addr, bool ra = false) {		\
    return *(type##_t*)(addr); \
  }
#define store_func(type, prefix, xlate_flags)		    \
  inline void prefix##_##type(long addr, type##_t val) {    \
    *(type##_t*)(addr) = val;				    \
  }
  
#define amo_func(type)						\
  template<typename op>	type##_t amo_##type(long addr, op f) {	\
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

  void acquire_load_reservation(long a);
  void yield_load_reservation();
  bool check_load_reservation(long a, long size);
  void flush_icache() { }
  void flush_tlb() { }
};

class cpu_t* find_cpu(int tid);
