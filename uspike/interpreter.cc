/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "hart.h"


inline uint64_t mulhu(uint64_t a, uint64_t b)
{
  uint64_t t;
  uint32_t y1, y2, y3;
  uint64_t a0 = (uint32_t)a, a1 = a >> 32;
  uint64_t b0 = (uint32_t)b, b1 = b >> 32;

  t = a1*b0 + ((a0*b0) >> 32);
  y1 = t;
  y2 = t >> 32;

  t = a0*b1 + y1;
  y1 = t;

  t = a1*b1 + y2 + (t >> 32);
  y2 = t;
  y3 = t >> 32;

  return ((uint64_t)y3 << 32) | y2;
}

inline int64_t mulh(int64_t a, int64_t b)
{
  int negate = (a < 0) != (b < 0);
  uint64_t res = mulhu(a < 0 ? -a : a, b < 0 ? -b : b);
  return negate ? ~res + (a * b == 0) : res;
}

inline int64_t mulhsu(int64_t a, uint64_t b)
{
  int negate = a < 0;
  uint64_t res = mulhu(a < 0 ? -a : a, b);
  return negate ? ~res + (a * b == 0) : res;
}









#define THREAD_STACK_SIZE  (1<<14)

option<>     conf_isa("isa",		"rv64imafdcv",			"RISC-V ISA string");
option<>     conf_vec("vec",		"vlen:128,elen:64,slen:128",	"Vector unit parameters");
option<long> conf_stat("stat",		100,				"Status every M instructions");
option<bool> conf_ecall("ecall",	false, true,			"Show system calls");
option<bool> conf_quiet("quiet",	false, true,			"No status report");


struct syscall_map_t {
  int sysnum;
  const char* name;
};

struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"  
};
const int highest_ecall_num = HIGHEST_ECALL_NUM;

void status_report()
{
  if (conf_quiet)
    return;
  double realtime = elapse_time();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", hart_t::total_count(), realtime, hart_t::total_count()/1e6/realtime);
  if (hart_t::threads() <= 16) {
    char separator = '(';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%1ld%%", separator, 100*p->executed()/hart_t::total_count());
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::threads() > 1)
    fprintf(stderr, "(%d cores)", hart_t::threads());
}

/* RISCV-V clone() system call arguments not same as X86_64:
   a0 = flags
   a1 = child_stack
   a2 = parent_tidptr
   a3 = tls
   a4 = child_tidptr
*/

void show(hart_t* cpu, long pc, FILE* f)
{
  Insn_t i = code.at(pc);
  int rn = i.rd()==NOREG ? i.rs2() : i.rd();
  long rv = cpu->read_reg(rn);
  if (rn != NOREG)
    fprintf(stderr, "%6ld: %4s[%16lx] ", cpu->tid(), reg_name[rn], rv);
  else
    fprintf(stderr, "%6ld: %4s[%16s] ", cpu->tid(), "", "");
  labelpc(pc);
  disasm(pc);
}

template<class T> bool hart_t::cas(long pc)
{
  Insn_t i = code.at(pc);
  T* ptr = (T*)read_reg(i.rs1());
  T expect  = read_reg(i.rs2());
  T replace = read_reg(i.rs3());
  T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
  write_reg(code.at(pc+4).rs1(), oldval);
  if (oldval == expect)  write_reg(i.rd(), 0);	/* sc was successful */
  return oldval == expect;
}

//extern long (*golden[])(long pc, mmu_t& MMU, class processor_t* p);

#undef RM
#define RM ({ int rm = i.immed(); \
              if(rm == 7) rm = fcsr.f.rm; \
              if(rm > 4) die("Illegal instruction"); \
              rm; })
#define srm  softfloat_roundingMode=RM
#define sfx  fcsr.f.flags |= softfloat_exceptionFlags;


#define fmin_s_body() \
      { \
	bool less = f32_lt_quiet(f32(f1), f32(f2)) || (f32_eq(f32(f1), f32(f2)) && (f32(f1).v & F32_SIGN)); \
	if (isNaNF32UI(f32(f1).v) && isNaNF32UI(f32(f2).v)) \
	  wfd(f32(defaultNaNF32UI)); \
	else \
	  wfd(less || isNaNF32UI(f32(f2).v) ? f1 : f2); \
      }

#define fmax_s_body() \
      { \
	bool greater = f32_lt_quiet(f32(f2), f32(f1)) || (f32_eq(f32(f2), f32(f1)) && (f32(f2).v & F32_SIGN)); \
	if (isNaNF32UI(f32(f1).v) && isNaNF32UI(f32(f2).v)) \
	  wfd(f32(defaultNaNF32UI)); \
	else \
	  wfd(greater || isNaNF32UI(f32(f2).v) ? f1 : f2); \
      }

#define fmin_d_body() \
      { \
	bool less = f64_lt_quiet(f64(f1), f64(f2)) || (f64_eq(f64(f1), f64(f2)) && (f64(f1).v & F64_SIGN)); \
	if (isNaNF64UI(f64(f1).v) && isNaNF64UI(f64(f2).v)) \
	  wfd(f64(defaultNaNF64UI)); \
	else \
	  wfd(less || isNaNF64UI(f64(f2).v) ? f1 : f2); \
      }

#define fmax_d_body() \
      { \
	bool greater = f64_lt_quiet(f64(f2), f64(f1)) || (f64_eq(f64(f2), f64(f1)) && (f64(f2).v & F64_SIGN)); \
	if (isNaNF64UI(f64(f1).v) && isNaNF64UI(f64(f2).v)) \
	  wfd(f64(defaultNaNF64UI)); \
	else \
	  wfd(greater || isNaNF64UI(f64(f2).v) ? f1 : f2); \
      }

#define r1n0  i.rs1()!=0

#define wrd(e)	xrf[i.rd()] = (e)
#define r1	xrf[i.rs1()]
#define r2	xrf[i.rs2()]
#define imm	i.immed()

#define wfd(e)	frf[i.rd()-FPREG] = freg(e)
#define f1	frf[i.rs1()-FPREG]
#define f2	frf[i.rs2()-FPREG]
#define f3	frf[i.rs3()-FPREG]

#define wpc(npc)  pc=npc


#define ebreak()  die("breakpoint not implemented");
#define ecall()  proxy_ecall(insns)
#define fence(n)  


template<typename T> inline T* MEM(long a) { return (T*)a; }

bool hart_t::interpreter(long how_many)
{
  //  processor_t* p = spike();
  //  long* xreg = reg_file();
  long pc = read_pc();
  long insns = 0;
#ifdef DEBUG
  long oldpc;
#endif
  do {
#ifdef DEBUG
    dieif(!code.valid(pc), "Invalid PC %lx, oldpc=%lx", pc, oldpc);
    oldpc = pc;
    debug.insert(executed()+insns+1, pc);
#endif

    Insn_t i = code.at(pc);
    switch ((Opcode_t)i.opcode()) {
    case Op_ZERO:  die("Op_ZERO opcode");

#include "semantics.h"
      
      
    case Op_cas12_w:  if (!cas<int32_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=12; break;
    case Op_cas12_d:  if (!cas<int64_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=12; break;
    case Op_cas10_w:  if (!cas<int32_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=10; break;
    case Op_cas10_d:  if (!cas<int64_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=10; break;

    case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
    case Op_UNKNOWN:  die("Op_UNKNOWN opcode");
    } // switch (i.opcode())
    xrf[0] = 0;
    
#ifdef DEBUG
    i = code.at(oldpc);
    int rn = i.rd()==NOREG ? i.rs2() : i.rd();
    debug.addval(i.rd(), read_reg(rn));
#endif
  } while (++insns < how_many);
  write_pc(pc);
  incr_count(insns);
  return false;
}

