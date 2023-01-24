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
#include "caveat.h"
#include "instructions.h"
#include "strand.h"

option<long> conf_report("report", 100000000, "Status report frequency");


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


struct syscall_map_t {
  int sysnum;
  const char* name;
};

struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"  
};
const int highest_ecall_num = HIGHEST_ECALL_NUM;

/* RISCV-V clone() system call arguments not same as X86_64:
   a0 = flags
   a1 = child_stack
   a2 = parent_tidptr
   a3 = tls
   a4 = child_tidptr
*/

//extern long (*golden[])(long pc, mmu_t& MMU, class processor_t* p);

#undef RM
#define RM ({ int rm = i->immed(); \
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

#define r1n0  i->rs1()!=0

#define wrd(e)	xrf[i->rd()] = (e)
#define r1	xrf[i->rs1()]
#define r2	xrf[i->rs2()]
#define imm	i->immed()

#define wfd(e)	frf[i->rd()-FPREG] = freg(e)
#define f1	frf[i->rs1()-FPREG]
#define f2	frf[i->rs2()-FPREG]
#define f3	frf[i->rs3()-FPREG]

#define stop  goto end_basic_block
#define wpc(npc)  pc=(npc)


#define ebreak()  die("breakpoint not implemented");
//#define ecall()  proxy_ecall(); wpc(pc+4); stop
#define fence(n)

//#define LOAD(typ, addr) *(typ*)(*ap++=addr)
//#define STORE(typ, addr, val) *(typ*)(*ap++=addr)=val

//#define LOAD(typ, addr) *(typ*)(addr)
//#define STORE(typ, addr, val) *(typ*)(addr)=(val)

#define LOAD(typ, addr) *Load<typ>(*ap++=addr)
#define STORE(typ, addr, val) Store<typ>(*ap++=addr, val)



void strand_t::interpreter(simfunc_t simulator, statfunc_t my_status)
{
  while (1) {			// once per basic block
    long begin_pc = pc;
    Insn_t* begin_i = code.descr(pc);
    Insn_t* i = begin_i;
    long count = 0;
    long* ap = addresses;
    while (2) {			// once per instruction
#ifdef DEBUG
      int8_t old_rd = i->rd();
      debug.insert(executed()+1, pc, i);
#endif
      count++;
      xrf[0] = 0;
      i = code.descr(pc);
      //print_trace(pc, i);
      switch (i->opcode()) {
      case Op_ZERO:  die("Op_ZERO opcode");

#include "semantics.h"		// jumps goto end_basic_block
	
      case Op_cas12_w:  wpc(!cas<int32_t>(pc) ? pc+code.at(pc+4).immed()+4 : pc+12); stop;
      case Op_cas12_d:  wpc(!cas<int64_t>(pc) ? pc+code.at(pc+4).immed()+4 : pc+12); stop;
      case Op_cas10_w:  wpc(!cas<int32_t>(pc) ? pc+code.at(pc+4).immed()+4 : pc+10); stop;
      case Op_cas10_d:  wpc(!cas<int64_t>(pc) ? pc+code.at(pc+4).immed()+4 : pc+10); stop;

      case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
      case Op_UNKNOWN:  die("Op_UNKNOWN opcode");
      } // switch
#ifdef DEBUG
      debug.addval(i->rd()!=NOREG ? xrf[i->rd()] : xrf[i->rs2()]);
#endif
      //      i += i->compressed() ? 1 : 2;
      //      dieif(i!=code.descr(pc), "i=%p != %p=code.descr(pc=%lx)", i, code.descr(pc), pc);
    } // while (2)
  end_basic_block:

#if 0
    {
      static long callstack[1024];
      static int top = 1;
      if (i->opcode()==Op_c_jalr || i->opcode()==Op_jal || i->opcode()==Op_jalr) {
	callstack[top++] = pc;
	fprintf(stderr, "%*s", 2*top, "");
	labelpc(pc, stderr);
	//	fprintf(stderr, "%08lx", pc);
	fprintf(stderr, "\n");
      }
      else if ((i->opcode()==Op_c_ret || i->opcode()==Op_ret) && i->rs1()==1)
	--top;
    }
#endif

#ifdef DEBUG
    debug.addval(xrf[i->rd()]);
#endif
    _executed += count;
    if (_executed >= next_report) {
      status_report(my_status);
      next_report += conf_report;
    }
    simulator(hart, begin_pc, begin_i, count, addresses);
  } // while (1)
}

void strand_t::single_step()
{
  abort();
}

