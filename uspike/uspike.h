#include <cassert>
#include <cstdint>
#include <iostream>
using namespace std;

#include "encoding.h"
#include "trap.h"
#include "arith.h"
#include "mmu.h"
#include "processor.h"

#include "opcodes.h"

struct Insn_t {
  unsigned op_4B	:  1;
  unsigned op_vm	:  1;
  unsigned op_longimmed	:  1;
  enum Opcode_t op_code	: 13;
  uint8_t op_rd;
  uint8_t op_r1;
  union {
    struct {
      uint8_t r2;
      uint8_t r3;
      int16_t imm;
    } op;
    int32_t op_immed;
  };
  Insn_t() { *((uint64_t*)this) = 0; }
  Insn_t(enum Opcode_t code, int big, int bigimm) { *((uint64_t*)this)=0; op_code=code; op_4B=big; op_longimmed=bigimm; }
};
static_assert(sizeof(Insn_t) == 8);

#define NOREG	0
#define VMREG	1
#define GPREG	VMREG+1
#define FPREG	GPREG+32
#define VPREG	FPREG+32
#define NUMREGS	VPREG+32

class insnSpace_t {
  long base;
  long limit;
  Insn_t* predecoded;
public:  
  insnSpace_t() { base=limit=0; predecoded=0; }
  void init(long lo, long hi);
  long index(long pc) { return (pc-base)/2; }
  Insn_t at(long pc) { return predecoded[index(pc)]; }
  uint32_t image(long pc) { assert(base<=pc && pc<limit); return *(uint32_t*)(pc); }
  friend long I_ZERO(long pc, processor_t* p);
};

extern insnSpace_t insn;
extern const char* op_name[];
extern const char* reg_name[];

void disasm(long pc, const char* end, FILE* f =stderr);
inline void disasm(long pc, FILE* f =stderr) { disasm(pc, "\n", f); }

extern long (*emulate[])(long pc, processor_t* p);
#undef set_pc_and_serialize
#define set_pc_and_serialize(x)
#undef serialize
#define serialize(x)
#define xlen 64

//typedef long (*emulate_t)(long pc, processor_t* p);
//extern emulate_t emulate[];

struct pctrace_t {
  long pc;
  reg_t val;
  uint8_t rn;
  pctrace_t() { pc=0; val=0; rn=0; }
  pctrace_t(long p, int n, reg_t v) { pc=p; val=v; rn=n; }
};

#define PCTRACEBUFSZ  (1<<6)
struct Debug_t {
  pctrace_t trace[PCTRACEBUFSZ];
  int cursor;
  pctrace_t get() { pctrace_t pt=trace[cursor]; cursor=(cursor+1)%PCTRACEBUFSZ; return pt; }
  void insert(pctrace_t pt)    { trace[cursor]=pt; cursor=(cursor+1)%PCTRACEBUFSZ; }
  void insert(long pc, int rn, reg_t val) { insert(pctrace_t(pc, rn, val)); }
  void print(FILE* f =stderr);
};

extern Debug_t debug;

extern "C" {
  long load_elf_binary( const char* file_name, int include_data );
  long initialize_stack(int argc, const char** argv, const char** envp, long entry);
  extern unsigned long low_bound, high_bound;
  void start_time(long mhz =1000000000);
  long proxy_ecall(long rvnum, long cycles, long a0, long a1, long a2, long a3, long a4, long a5);
};

Insn_t decoder(long pc);
void run_insns(processor_t* p, long count);

extern processor_t* gdbCPU;

void OpenTcpLink(const char* name);
void ProcessGdbCommand();

/*
  Utility stuff.
*/
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }

