/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include "opcodes.h"

#define die(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }

extern const char* op_name[];
extern const char* reg_name[];
extern const ISA_bv_t required_isa[];
extern const ATTR_bv_t attributes[];

#define GPREG	0
#define FPREG	(GPREG+32)
#define VPREG	(FPREG+32)
#define VMREG	(VPREG+32)
#define NOREG	-1

class alignas(8) Insn_t {
  Opcode_t op_code;
  int8_t op_rd;
  int8_t op_rs1;
  union {
    struct {
      int16_t imm;
      int8_t rs2;
      int8_t rs3;
    } op;
    int32_t op_longimm;
  };
  void setimm(int16_t v) { op.imm=(v<<1)|0x1; }
public:
  bool longimmed() { return (op.imm & 0x1) == 0; }
  long immed() { return longimmed() ? op_longimm : op.imm>>1; }

  long opcode() { return op_code; }
  int rd()  { return op_rd; }
  int rs1() { return op_rs1; }
  int rs2() { return op.rs2; }
  int rs3() { return op.rs3; }
  bool compressed() { return op_code <= Last_Compressed_Opcode; }
  
  Insn_t() { *((int64_t*)this) = -1; } // all registers become NOREG
  Insn_t(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3, int16_t imm);
  Insn_t(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);
  Insn_t(Opcode_t code, int8_t rd, int8_t rs1, int32_t longimmed);
  Insn_t(Opcode_t code, int8_t rd, int32_t longimmed);
};
static_assert(sizeof(Insn_t) == 8);


struct bb_header_t {
  uint16_t count;
  long addr : 48;
};
static_assert(sizeof(bb_header_t) == 8);

struct bb_footer_t {
  uint32_t taken;
  uint32_t fallthru;
};
static_assert(sizeof(bb_footer_t) == 8);
  



Insn_t decoder(long pc);

class alignas(4096) Isegment_t {
  //  enum SegType_t typ;
 public:
  Insn_t* end;			// first free entry
  bb_header_t* bb;		// current basic block;
  bb_header_t* new_basic_block(long pc) { bb=(bb_header_t*)end++; bb->addr=pc; return bb; }
  void add_insn(Insn_t insn) { *end++ = insn; }
  void end_basic_block() { bb->count = end++ - (Insn_t*)bb; }
};





















typedef void (*simfunc_t)(class hart_t* p, long pc, Insn_t* begin, long count, long* addresses);
typedef void (*statfunc_t)(class hart_t* p);

class hart_t {
  
  static volatile hart_t* cpu_list;	// for find() using thread id
  volatile hart_t* link;		// list of strand_t
  void attach_hart();
  int my_tid;				// my Linux thread number
  int _number;				// index of this hart
  static volatile int num_threads;	// allocated

public:
  class strand_t* strand;	// opaque pointer
  
  hart_t(hart_t* from);
  hart_t(int argc, const char* argv[], const char* envp[]);
  
  //  virtual void simulator(long pc, Insn_t* begin, long count, long* addresses);
  //  virtual void my_report(long total);
  
  void interpreter(simfunc_t simulator, statfunc_t my_status);
  long executed();
  static long total_count();
  
  static hart_t* list() { return (hart_t*)cpu_list; }
  hart_t* next() { return (hart_t*)link; }
  int number() { return _number; }
  long tid() { return my_tid; }
  void set_tid();
  static hart_t* find(int tid);
  static int threads() { return num_threads; }
      
  void print_debug_trace();  
};

void start_time();
double elapse_time();
void status_report(statfunc_t s);
