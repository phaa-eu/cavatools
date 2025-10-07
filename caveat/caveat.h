/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <pthread.h>

#include "options.h"
#include "opcodes.h"

#define dbmsg(fmt, ...)		       { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n"); }
#define die(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(-1); }

extern option<size_t> conf_tcache;	// translation cache size
extern option<size_t> conf_hash;	// basic block hash table size, best if prime

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
#define NOREG	0xFF

class alignas(8) Insn_t {
  Opcode_t op_code;
  uint8_t op_rd;
  uint8_t op_rs1;
  union {
    struct {
      int16_t imm;
      uint8_t rs2;
      uint8_t rs3;
    } op;
    int32_t op_longimm;
  };
  void setimm(int16_t v) { op.imm=(v<<1)|0x1; }
public:
  bool longimmed() const { return (op.imm & 0x1) == 0; }
  long immed() const { return longimmed() ? op_longimm : op.imm>>1; }

  Opcode_t opcode() const { return op_code; }
  uint rd()  const { return op_rd; }
  uint rs1() const { return op_rs1; }
  uint rs2() const { return op.rs2; }
  uint rs3() const { return op.rs3; }
  bool compressed() const { return op_code <= Last_Compressed_Opcode; }

  friend Insn_t decoder(uintptr_t pc);
  friend void substitute_cas(uintptr_t pc, Insn_t* i3);
};
static_assert(sizeof(Insn_t) == 8);

Insn_t decoder(uintptr_t pc);

/*
  The Translation Cache is an array of 64-bit slots.  A slot can be a translated
  instruction, a basic block header (2 words), or a branch target pointer.  Translated
  instructions are described above.  Each basic block begins with a header
  (defind below) followed by one or more translated instructions.  Each basic block
  is terminated by a link pointer to the next basic block's header.
  
  Links are always confirmed by computed branch target pc.  Thus they act as branch
  predictors for jump-register instructions.  Basic blocks that end in a conditional
  branch have two pointers, the branch-taken target followed by the fall-thru target.
*/

struct alignas(8) Header_t {
  uintptr_t addr;		// basic block beginning address
  bool conditional	:  1;	// end in conditional branch
  unsigned count	:  7;	// number of instructions
  unsigned length	:  8;	// number of 16-bit parcels
  unsigned pad		: 16;
  uint32_t link;		// offset to next hash table entry
  Header_t(uintptr_t a, unsigned l, unsigned c, bool p) { addr=a; length=l; count=c; conditional=p; link=0; }
};
static_assert(sizeof(Header_t) == 16);

struct Tentry_t {
  uint64_t never_directly_accessed;
};

static inline Insn_t* insnp(Tentry_t* p) { return (Insn_t*)p; }
static inline Insn_t* insnp(Header_t* p) { return (Insn_t*)p; }

static inline Header_t* bbptr(Tentry_t* p) { return (Header_t*)p; }
static inline Header_t* bbptr(Insn_t*   p) { return (Header_t*)p; }

static inline Tentry_t* tcptr(Header_t* p) { return (Tentry_t*)p; }
static inline Tentry_t* tcptr(Insn_t*   p) { return (Tentry_t*)p; }

class Tcache_t {
public:
  Tentry_t* array;		// array of headers, instructions, link pointers
  size_t _size;			// current number of entries
  // hash table links are integer index relative to array
  uint32_t* table;		// hash table for finding basic blocks by address
  uint32_t hashfunction(uintptr_t pc) { return pc % conf_hash(); }
  size_t _flushed;		// how many times
  
  class Counters_t* list;	// of counter arrays
  friend class Counters_t;
  
public:
  size_t size() { return _size; }
  size_t flushed() { return _flushed; }
  
  uint32_t index(Header_t* bb) { return tcptr(bb)-array; }
  uint32_t index(Insn_t*    i) { return tcptr( i)-array; }
  
  // look in hash table for basic block addr==pc
  Header_t* find(uintptr_t pc) {
    Header_t* bb = bbptr(&array[table[hashfunction(pc)]]);
    while (bb != bbptr(array)) {
      if (bb->addr == pc) return bb;
      bb = bbptr(&array[bb->link]);
    }
    return 0;
  }
  // copy basic block into cache and insert into hash table
  Header_t* add(Header_t* wbb, size_t n) {
    if (_size+n > conf_tcache()) {
      dieif(n>conf_tcache(), "basic block size %lu bigger than cache %lu", n, conf_tcache());
      clear();
    }
    Header_t* bb = (Header_t*)&array[_size];
    _size += n;
    memcpy(bb, wbb, n*sizeof(uint64_t));
    uint32_t h = hashfunction(bb->addr);
    bb->link = table[h];
    table[h] = index(bb);
    return bb;
  }
  // flush translation cache
  void clear() {
    memset((void*)table, 0, conf_hash()*sizeof(uint32_t));
    _size = 2;			// not necessary to zero cache
    _flushed++;
  }
  Tcache_t() {
    array = new Tentry_t[conf_tcache()];
    table = new uint32_t[conf_hash()];
    memset((void*)array, 0, conf_tcache()*sizeof(Tentry_t));
    memset((void*)table, 0, conf_hash()*sizeof(uint32_t));
    _size = 2;
    _flushed = 0;
  }
};


typedef void (*simfunc_t)(class hart_t* h, Header_t* bb, uintptr_t* ap);
//typedef uintptr_t (*syscallfunc_t)(class hart_t* h, int num,
//				   uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
typedef void (*syscallfunc_t)(class hart_t* h);
typedef int (*clonefunc_t)(class hart_t* h);
typedef void (*interpreterfunc_t)(class hart_t* h);


void start_time();
double elapse_time();
void controlled_by_gdb(const char* host_port, hart_t* cpu, simfunc_t simulator);

//const char* func_name(uintptr_t pc);


int slabelpc(char* buf, uintptr_t pc);
void labelpc(uintptr_t pc, FILE* f =stderr);
int sdisasm(char* buf, uintptr_t pc, const Insn_t* i);
void disasm(uintptr_t pc, const Insn_t* i, const char* end ="\n", FILE* f =stderr);

#if 0
int clone_thread(hart_t* s);
uintptr_t host_syscall(int sysnum, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
uintptr_t default_syscall_func(hart_t* h, int num, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
#endif
