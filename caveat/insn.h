/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifdef __cplusplus
extern "C" {
#endif

  
typedef long Addr_t;
#define GEN_SEGV  (*((char*)0) = 0)


struct reg_t {
  union {
    int64_t l;
    uint64_t ul;
    int32_t i;
    uint32_t ui;
    double d;
    float f;
    void* p;
    Addr_t a;
#ifdef SOFT_FP
    float32_t f32;
    float64_t f64;
#endif
  };
};

struct insn_t {
  enum Opcode_t op_code : 16;
  uint8_t op_rd;		// 0..31 = integer registers
  uint8_t op_rs1;		// 32..63 = floating point registers
  union {
    int32_t op_constant;
    struct {
      int16_t immed;
      uint8_t rs2;		// 64 = not valid
      uint8_t rs3;		// only for floating multiply-add
    } op;
  };
};

#define ZERO  0			// Register x0 always zero
#define RA    1			// Standard RISC-V ABI convention
#define SP    2
#define GP    3
#define TP    4
#define NOREG 64


struct insnAttr_t {
  const char* name;		/* asserbler opcode */
  unsigned int  flags;		/* upper case, from Instructions.def */
  enum units_t unit : 8;	/* lower case, functional units */
  unsigned char latency;	/* filled in by simulator */
};  

  
struct insnSpace_t {
  struct insn_t* insn_array;
  Addr_t base, bound;
};

  

extern struct insnSpace_t insnSpace;
extern struct insnAttr_t insnAttr[];	/* Attribute array indexed by Opcode_t */


  
void insnSpace_init( Addr_t low, Addr_t high );
void decode_instruction( const struct insn_t* p, Addr_t PC );

Addr_t load_elf_binary( const char* file_name, int include_data );
Addr_t initialize_stack( int argc, const char** argv, const char** envp );
int find_symbol( const char* name, Addr_t* begin, Addr_t* end );
int find_pc( long pc, const char** name, long* offset );



static inline const struct insn_t* insn(Addr_t pc)
{
  return &insnSpace.insn_array[(pc-insnSpace.base)/2];
}

static inline int valid_pc(Addr_t pc)
{
  return insnSpace.base <= pc && pc < insnSpace.bound;
}

static inline void insert_breakpoint(Addr_t pc)
{
  assert(valid_pc(pc));
  struct insn_t* p = &insnSpace.insn_array[(pc-insnSpace.base)/2];
  if (shortOp(p->op_code))
    p->op_code = Op_c_ebreak;
  else
    p->op_code = Op_ebreak;
}


int find_symbol( const char* name, long* begin_addr, long* end_addr);
/*
  returns TRUE or FALSE found in ELF symbol table
  name		- function or variable
  begin_addr	- pointer to where addresses will be written
  end_addr	- writes NULL if symbol not found
*/


void print_symbol( long address, FILE* output_file );
/*
  Prints address as name+hex
  address	- in text or data segment
  output_file	- writes to this file
*/


int print_pc( long pc, FILE* output_file );
/*
  Print symbolic program counter
  returns whether address associated with known function
  pc		- program counter in text segment
  file_descr	- write to this file descriptor
*/

void print_insn( long address, FILE* output_file );
/*
  Disassemble instruction and print
  address	- program counter in text segment
  file_descr	- write to this file descriptor
*/

void print_registers( struct reg_t regs[], FILE* output_file );
/*  
  Print register files
  regs		- register files, IR+FR
  file_descr	- write to this file descriptor
*/


#ifdef __cplusplus
}
#endif
