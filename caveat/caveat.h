/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include "opcodes.h"

#define die(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(-1); }

extern const char* op_name[];
extern const char* reg_name[];
extern const ISA_bv_t required_isa[];
extern const ATTR_bv_t attributes[];
extern const uint64_t stop_before[];
extern const uint64_t stop_after[];

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

  Opcode_t opcode() { return op_code; }
  int rd()  { return op_rd; }
  int rs1() { return op_rs1; }
  int rs2() { return op.rs2; }
  int rs3() { return op.rs3; }
  bool compressed() { return op_code <= Last_Compressed_Opcode; }

  friend Insn_t decoder(long pc);
  friend void substitute_cas(long pc, Insn_t* i3);
};
static_assert(sizeof(Insn_t) == 8);

Insn_t decoder(long pc);

/*
  The Translation Cache is an array of 64-bit slots.  A slot can be a translated
  instruction, a basic block header, or a branch target pointer.  Translated
  instructions are described above.  Each basic block begins with a header
  (defind below) followed by one or more translated instructions.  Each basic block
  is terminated by a link (index) to the next basic block's header.  If the link
  is  zero the target is unknown.  Links are always confirmed by computed branch
  target pc.  Thus they act as branch predictors for jump-register instructions.
  Basic blocks that end in a conditional branch have two pointers, the branch-taken
  target followed by the fall-thru target.

  The first slot (index zero) of the Translation Cache contains number of active slots.
*/

struct Header_t {
  //  uint16_t count;
  bool branch		:  1;
  bool conditional	:  1;
  uint16_t count	: 14;
  long addr		: 48;
};
static_assert(sizeof(Header_t) == 8);

/*
  We can freely convert between pointers to instructions, headers and links.
*/
inline Header_t* bbp(void* p) { return (Header_t*)p; }
inline Insn_t* insnp(void* p) { return (Insn_t*)p; }
inline Header_t** linkp(void* p)   { return (Header_t**)p; }

extern Insn_t* tcache; // only interpreter allowed to change instructions
inline long tcache_size() { return *(long*)tcache; }
inline long index(Insn_t* p) { return p-tcache; }
inline long index(Header_t* p) { return insnp(p)-tcache; }

typedef void (*simfunc_t)(class hart_base_t* h, Header_t* bb);

class hart_base_t {
  class strand_t* strand;	// opaque pointer
  friend void controlled_by_gdb(const char* host_port, hart_base_t* cpu, simfunc_t simulator);
  
public:
  uint64_t* _counters;		// performance counter array (matching tcache)
  uint64_t* counters() { return _counters; }
  
  hart_base_t(hart_base_t* from);
  hart_base_t(int argc, const char* argv[], const char* envp[], bool counting =false);
  
  bool interpreter(simfunc_t simulator);
  bool single_step(simfunc_t simulator, bool show_trace =false);
  long* addresses();

  long pc();

  static hart_base_t* list();
  hart_base_t* next();
  int number();
  long tid();
  void set_tid();
  static hart_base_t* find(int tid);
  static int num_harts();
  void debug_print();
  
  void print_debug_trace();  
};

void start_time();
double elapse_time();
void controlled_by_gdb(const char* host_port, hart_base_t* cpu, simfunc_t simulator);

const char* func_name(long pc);


int slabelpc(char* buf, long pc);
void labelpc(long pc, FILE* f =stderr);
int sdisasm(char* buf, long pc, Insn_t* i);
void disasm(long pc, Insn_t* i, const char* end ="\n", FILE* f =stderr);
