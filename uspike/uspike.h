/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cassert>
#include <cstdint>
#include <stdio.h>
#include <string.h>

using namespace std;
void* operator new(size_t size);
void operator delete(void*) noexcept;

/*
  Utility stuff.
*/
#define die(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }

#define diesegv()  (*(char*)0=1)
#define checkif(bad) if (!(bad)) diesegv()

extern "C" {
  long load_elf_binary(const char* file_name, int include_data);
  int elf_find_symbol(const char* name, long* begin, long* end);
  const char* elf_find_pc(long pc, long* offset);
  long initialize_stack(int argc, const char** argv, const char** envp);
  long emulate_brk(long addr);
  extern unsigned long low_bound, high_bound;
  void redecode(long pc);
};

extern option<>     conf_isa;
extern option<>     conf_vec;
extern option<long> conf_stat;
extern option<bool> conf_ecall;
extern option<bool> conf_quiet;
extern option<long> conf_show;
extern option<>     conf_gdb;

#include "opcodes.h"

// The bits[63:32] are a union with two different length immediates
// For short immediates a 13-bit value is in [47:35] (right shfit by 3)
// There are 3 flag bits in [34:32].  Bit[32]=0 indicates long immediate.
// Long immediates always have zeros in low order bits.  We take advantage
// by making sure flag bits are all zero in this case, then just use value.

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
  bool compressed() { return op_code <= Last_Compressed_Opcode; }
  bool longimmed() { return (op.imm & 0x1) == 0; }
  friend Insn_t reg1insn( Opcode_t code, int8_t rd, int8_t rs1);
  friend Insn_t reg2insn( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2);
  friend Insn_t reg3insn (Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3);
  friend Insn_t reg0imm(Opcode_t code, int8_t rd, int32_t longimmed);
  friend Insn_t reg1imm(Opcode_t code, int8_t rd, int8_t rs1, int16_t imm);
  friend Insn_t reg2imm(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);
};
static_assert(sizeof(Insn_t) == 8);

Insn_t reg1insn( Opcode_t code, int8_t rd, int8_t rs1);
Insn_t reg2insn( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2);
Insn_t reg3insn(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3);
Insn_t reg0imm( Opcode_t code, int8_t rd, int32_t longimmed);
Insn_t reg1imm( Opcode_t code, int8_t rd, int8_t rs1, int16_t imm);
Insn_t reg2imm( Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm);

#define GPREG	0
#define FPREG	GPREG+32
#define VPREG	FPREG+32
#define VMREG	VPREG+32
#define NOREG	-1

Insn_t decoder(int b, long pc);	// given bitpattern image of in struction

//enum stop_reason { stop_normal, stop_exited, stop_breakpoint };
//enum stop_reason interpreter(class cpu_t* mycpu, long number);
void interpreter(class cpu_t* cpu);

long emulate(long pc, cpu_t* cpu);
#define STOP_breakpoint -1
#define STOP_exited     -2

void status_report();
class cpu_t* initial_cpu(long entry, long sp);
void show_insn(long pc, int tid);

static inline bool find_symbol(const char* name, long &begin, long &end) { return elf_find_symbol(name, &begin, &end) != 0; }
static inline const char* find_pc(long pc, long &offset) { return elf_find_pc(pc, &offset); }

class insnSpace_t {
  long _base;
  long _limit;
  class Insn_t* predecoded;
public:  
  insnSpace_t() { _base=_limit=0; predecoded=0; }
  void init(long lo, long hi);
  bool valid(long pc) { return _base<=pc && pc<_limit; }
  long index(long pc) { checkif(valid(pc)); return (pc-_base)/2; }
  Insn_t at(long pc) { return predecoded[index(pc)]; }
  Insn_t* descr(long pc) { return &predecoded[index(pc)]; }
  uint32_t image(long pc) { checkif(valid(pc)); return *(uint32_t*)(pc); }
  Insn_t set(long pc, Insn_t i) { predecoded[index(pc)] = i; return i; }
  long base() { return _base; }
  long limit() { return _limit; }
};

extern insnSpace_t code;
extern const char* op_name[];
extern const char* reg_name[];

void substitute_cas(long lo, long hi);
int slabelpc(char* buf, long pc);
void labelpc(long pc, FILE* f =stderr);
int sdisasm(char* buf, long pc);
void disasm(long pc, const char* end, FILE* f =stderr);
inline void disasm(long pc, FILE* f =stderr) { disasm(pc, "\n", f); }
void show(cpu_t* cpu, long pc, FILE* f =stderr);

void start_time();
double elapse_time();
double simulated_time(long cycles);
