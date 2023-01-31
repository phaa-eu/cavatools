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

Insn_t decoder(long pc);

/*
  The Translation Cache consists of a header and an array of 64-bit slots.
  A slot can be a translated instruction, a basic block header, or a branch target pointer.
  Translated instructions are described above.  Each basic block begins with a header,
  defind below, followed by one or more translated instructions.  Each basic block is
  terminated by a pointer to the header of the next basic block.  This pointer is NULL if
  the target is unknown, for example a jump register instruction.  Basic blocks that end
  in a conditional branch have two pointers, the branch-taken target followed by the
  fall-thru target.
*/

struct bb_header_t {
  uint16_t count;
  long addr : 48;
};
static_assert(sizeof(bb_header_t) == 8);

extern Insn_t* tcache;			// Translated instructions and basic block info



class hart_t {
  class strand_t* strand;	// opaque pointer
  long _executed;			// executed this thread
  long next_report;

public:
  
  hart_t(hart_t* from);
  hart_t(int argc, const char* argv[], const char* envp[]);
  
  virtual void simulator(long pc, Insn_t* begin, long count, long* addresses);
  void interpreter();

  long executed();
  static long total_count();
  static hart_t* list();
  hart_t* next();
  int number();
  long tid();
  void set_tid();
  static hart_t* find(int tid);
  static int threads();
  void debug_print();
  
  void print_debug_trace();  
};

void start_time();
double elapse_time();
