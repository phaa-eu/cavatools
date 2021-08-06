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

typedef uint64_t Addr_t;

union Insn_t {
  struct {
    int op_4B			:  1;
    int op_vm			:  1;
    enum Opcode_t op_code	: 14;
    uint8_t op_rd;
    uint8_t op_rs1;
    union {
      struct {
	uint8_t rs2;
	uint8_t rs3;
	int16_t imm;
      } op;
      int32_t op_immed;
    };
  };
  uint64_t v;
  Insn_t() { v=0; }
  void disasm(Addr_t pc, const char* end, FILE* f =stderr) const;
  void disasm(Addr_t pc, FILE* f =stderr) { disasm(pc, "\n", f); }
};
static_assert(sizeof(Insn_t) == 8);

#define NOREG	0
#define VMREG	1
#define GPREG	VMREG+1
#define FPREG	GPREG+32
#define VPREG	FPREG+32
#define NUMREGS	VPREG+32

struct insnSpace_t {
  Addr_t base;
  Addr_t limit;
  Insn_t* predecoded;
  uint32_t image(Addr_t pc) { assert(base<=pc && pc<limit); return *(uint32_t*)(pc); }
  insnSpace_t() { base=limit=0; predecoded=0; }
  void predecode(long lo, long hi);
  const Insn_t* insn(long pc) { return &predecoded[(pc-base)/2]; }
  void disasm(Addr_t pc, const char* end, FILE* f =stderr) { insn(pc)->disasm(pc, end, f); }
  void disasm(Addr_t pc, FILE* f =stderr) { disasm(pc, "\n", f); }
};

extern insnSpace_t insn;
extern const char* opcode_name[];
extern const char* reg_name[];

extern long (*emulate[])(long pc, processor_t* p);
#undef set_pc_and_serialize
#define set_pc_and_serialize(x)
#define xlen 64

//typedef Addr_t (*emulate_t)(Addr_t pc, processor_t* p);
//extern emulate_t emulate[];

struct pctrace_t {
  Addr_t pc;
  reg_t val;
  uint8_t rn;
  pctrace_t() { pc=0; val=0; rn=0; }
  pctrace_t(Addr_t p, int n, reg_t v) { pc=p; val=v; rn=n; }
};

#define PCTRACEBUFSZ  (1<<6)
struct Debug_t {
  pctrace_t trace[PCTRACEBUFSZ];
  int cursor;
  pctrace_t get() { pctrace_t pt=trace[cursor]; cursor=(cursor+1)%PCTRACEBUFSZ; return pt; }
  void insert(pctrace_t pt)    { trace[cursor]=pt; cursor=(cursor+1)%PCTRACEBUFSZ; }
  void insert(Addr_t pc, int rn, reg_t val) { insert(pctrace_t(pc, rn, val)); }
  void print(FILE* f =stderr);
};

extern Debug_t debug;

void load_elf_binary(const char* file_name, long &entry, long &low_bound, long &high_bound, bool include_data =true);
long initialize_stack(int argc, const char** argv, const char** envp, long entry);
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

