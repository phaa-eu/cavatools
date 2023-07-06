/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>

#include <pthread.h>

#include "opcodes.h"

#define dbmsg(fmt, ...)		       { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n"); }
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
  bool longimmed() const { return (op.imm & 0x1) == 0; }
  long immed() const { return longimmed() ? op_longimm : op.imm>>1; }

  Opcode_t opcode() const { return op_code; }
  int rd()  const { return op_rd; }
  int rs1() const { return op_rs1; }
  int rs2() const { return op.rs2; }
  int rs3() const { return op.rs3; }
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
  bool conditional	:  1;	// end in conditional branch
  unsigned count	:  7;	// number of instructions
  unsigned length	:  8;	// number of 16-bit parcels
  uintptr_t addr	: 48;	// beginning address
  Header_t* next;		// in hash table bucket
  //  Header_t(uintptr_t a, unsigned l, unsigned c, bool p, Header_t* n) { addr=a; length=l; count=c; conditional=p; next=n; }
};
static_assert(sizeof(Header_t) == 16);

class Tcache_t {
public:
  uint64_t* array;		// array of headers, instructions, link pointers
  volatile size_t _size;	// current number of entries
  
  size_t _extent;		// maximum number of entries in cache
  class Counters_t* list;	// of counter arrays

  Header_t** table;		// hash table for finding basic blocks by address
  uint64_t _hashsize;		// hash table size
  size_t hashfunction(uint64_t pc) { return pc % _hashsize; }
  
  friend class Counters_t;
public:
#if 0
  //  pthread_mutex_t lock;
  volatile int running;	// how many threads currently running
  volatile int pause;	// tell threads to pause for synchronous event
#endif

  void initialize(size_t cachesize, size_t hashtablesize);
  
  unsigned extent() { return _extent; }
  volatile unsigned size() { return _size; }
  
  unsigned index(const Header_t* bb) { return (uint64_t*)bb - array; }
  const Header_t* bbptr(size_t k) { dieif(k>_size, "cache index %lu out of bounds %lu", k, _size); return (const Header_t*)&array[k]; }

  Header_t* find(uintptr_t pc);
  void insert(Header_t* bb);
  void clear();
  Header_t* add(Header_t* begin, size_t entries);
};

extern Tcache_t tcache;		// global to all threads

class Counters_t {
  uint64_t* array;		// counters parallel tcache
  Counters_t* next;		// so tcache can find us
  friend class Tcache_t;
public:
  Counters_t() { array=new uint64_t[tcache.extent()]; next=tcache.list; tcache.list=this; }
  const uint64_t* ptr(unsigned k) { dieif(k>tcache.size(), "cache index %u out of bounds", k); return &array[k]; }
  uint64_t* wptr(unsigned k) { dieif(k>tcache.size(), "cache index %u out of bounds", k); return &array[k]; }
  uint64_t operator[](unsigned k) const { dieif(k>tcache.size(), "cache index %u out of bounds", k); return array[k]; }
};

static inline const Insn_t* insnp(uint64_t p) { return (const Insn_t*)p; }
static inline const Insn_t* insnp(const Header_t* p) { return (const Insn_t*)p; }
static inline const Header_t* bbp(uint64_t p) { return (const Header_t*)p; }
static inline const Header_t* bbp(const Insn_t* p) { return (const Header_t*)p; }


typedef void (*simfunc_t)(class hart_base_t* h, long index);
typedef long (*syscallfunc_t)(class hart_base_t* h, long num, long* args);
typedef long (*clonefunc_t)(class hart_base_t* h, long* args);

class hart_base_t {
  class strand_t* strand;	// opaque pointer
  
  friend void controlled_by_gdb(const char* host_port, hart_base_t* cpu);
  friend int clone_thread(hart_base_t* s);
  
public:
  Counters_t counters;		// counting array (1:1 with tcache)
  
  simfunc_t simulator;		// function pointer for simulation
  clonefunc_t clone;		// function pointer just for clone system call
  syscallfunc_t syscall;	// function pointer for other system calls
  
  hart_base_t(int argc, const char* argv[], const char* envp[]);
  hart_base_t(hart_base_t* from);
  static void join_all();
  
  bool interpreter();
  bool single_step(bool show_trace =false);
  uintptr_t* addresses();
  uintptr_t pc();

  static hart_base_t* list();
  hart_base_t* next();
  int number();
  int tid();
  void set_tid();
  static hart_base_t* find(int tid);
  static int num_harts();
  void debug_print();
  
  void print_debug_trace();  
};


void start_time();
double elapse_time();
void controlled_by_gdb(const char* host_port, hart_base_t* cpu, simfunc_t simulator);

//const char* func_name(uintptr_t pc);


int slabelpc(char* buf, uintptr_t pc);
void labelpc(uintptr_t pc, FILE* f =stderr);
int sdisasm(char* buf, uintptr_t pc, const Insn_t* i);
void disasm(uintptr_t pc, const Insn_t* i, const char* end ="\n", FILE* f =stderr);

int clone_thread(hart_base_t* s);
long host_syscall(int sysnum, long* a);
long default_syscall_func(class hart_base_t* h, long num, long* args);

void wait_until_zero(volatile int* vp);
void release_waiter(volatile int* vp);

