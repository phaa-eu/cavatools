/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#include "opcodes.h"
extern const char* op_name[];
extern const char* reg_name[];

// The bits[63:32] are a union with two different length immediates
// For short immediates a 13-bit value is in [47:35] (right shfit by 3)
// There are 3 flag bits in [34:32].  Bit[32]=0 indicates long immediate.
// Long immediates always have zeros in low order bits.  We take advantage
// by making sure flag bits are all zero in this case, then just use value.

#define GPREG	0
#define FPREG	GPREG+32
#define VPREG	FPREG+32
#define VMREG	VPREG+32
#define NOREG	-1

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
  Insn_t(Opcode_t code, int8_t rd, int16_t imm)     :Insn_t(code)  { op_rd=rd; op.imm=imm<<3|0x1; }
  long opcode() { return op_code; }
  int rd()  { return op_rd; }
  int rs1() { return op_rs1; }
  int rs2() { return op.rs2; }
  int rs3() { return op.rs3; }
  long immed() { return (op.imm&0x1) ? op.imm>>3 : op_longimm; }
  int bytes() { return (op_code <= Last_Compressed_Opcode) ? 2 : 4; }
  bool longimmed() { return (op.imm & 0x1) == 0; }
  friend Insn_t reg1insn( Opcode_t code, int8_t rd, int8_t rs1);
  friend Insn_t reg2insn( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2);
  friend Insn_t reg3insn (Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3);
  friend Insn_t reg0imm(Opcode_t code, int8_t rd, int32_t longimmed);
  friend Insn_t reg1imm(Opcode_t code, int8_t rd, int8_t rs1, int16_t imm);
  friend Insn_t reg2imm(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);
};
static_assert(sizeof(Insn_t) == 8);

class insnSpace_t {
  long _base;
  long _limit;
  long _entry;
  class Insn_t* predecoded;
public:  
  void loadelf(const char* elfname);
  long base() { return _base; }
  long limit() { return _limit; }
  long entry() { return _entry; }
  
  bool valid(long pc) { return _base<=pc && pc<_limit; }
  long index(long pc) { checkif(valid(pc)); return (pc-_base)/2; }
  Insn_t at(long pc) { return predecoded[index(pc)]; }
  Insn_t* descr(long pc) { return &predecoded[index(pc)]; }
  uint32_t image(long pc) { checkif(valid(pc)); return *(uint32_t*)(pc); }
  Insn_t set(long pc, Insn_t i) { predecoded[index(pc)] = i; return i; }
};

/*
Insn_t reg1insn( Opcode_t code, int8_t rd, int8_t rs1);
Insn_t reg2insn( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2);
Insn_t reg3insn(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3);
Insn_t reg0imm( Opcode_t code, int8_t rd, int32_t longimmed);
Insn_t reg1imm( Opcode_t code, int8_t rd, int8_t rs1, int16_t imm);
Insn_t reg2imm( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);
*/

Insn_t decoder(int b, long pc);	// given bitpattern image of in struction

extern insnSpace_t code;

void substitute_cas(long lo, long hi);
int slabelpc(char* buf, long pc);
void labelpc(long pc, FILE* f =stderr);
int sdisasm(char* buf, long pc);
void disasm(long pc, const char* end, FILE* f =stderr);
inline void disasm(long pc, FILE* f =stderr) { disasm(pc, "\n", f); }
