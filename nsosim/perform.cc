/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <signal.h>

#include <cassert>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"
#include "arithmetic.h"

#include "components.h"
#include "memory.h"
#include "core.h"
	
#ifdef SPIKE
#error "Spike linkage not support at present"
#endif

#undef READ_REG
#undef READ_FREG
#undef WRITE_REG
#undef WRITE_FREG

#define READ_REG(n)   s.reg[n].x
#define READ_FREG(n)  s.reg[n].f
#define WRITE_REG(n, v)   s.reg[n].x = (v)
#define WRITE_FREG(n, v)  s.reg[n].f = (v)

void substitute_cas(uintptr_t pc, Insn_t* i3);

inline float  m32(freg_t x) { union { freg_t r; float  f; } cv; cv.r=x; return cv.f; }
inline double m64(freg_t x) { union { freg_t r; double f; } cv; cv.r=x; return cv.f; }
inline float32_t n32(float  x)  { union { float32_t t; float  f; } cv; cv.f=x; return cv.t; }
inline float64_t n64(double x)  { union { float64_t t; double f; } cv; cv.f=x; return cv.t; }

Addr_t Core_t::perform(Insn_t* i, Addr_t pc, History_t* h)
{
  uintptr_t addrbuf[100];
  uintptr_t* ap = addrbuf;
  count_insn();
  //WRITE_REG(0, 0);
#if 0
  debug.insert(pc, *i);
  labelpc(pc);
  disasm(pc, i);
#endif
  /*
    Abbreviations to keep isa.def semantics short
  */
#define wrd(e)	s.reg[i->rd()].x = (e)
#define r1	s.reg[i->rs1()].x
#define r2	s.reg[i->rs2()].x
#define r3	s.reg[i->rs3()].x
#define imm	i->immed()
#define wfd(e)	s.reg[i->rd()].f = freg(e)
#define f1	s.reg[i->rs1()].f
#define f2	s.reg[i->rs2()].f
#define f3	s.reg[i->rs3()].f

#define w32(e)	s.reg[i->rd()].f = freg(n32(e)) 
#define w64(e)	s.reg[i->rd()].f = freg(n64(e))
	

#ifdef VERIFY

  // these instructions cannot execute twice, so just use uspike value  
#define LOAD(T, a)			h->expected_rd
#define STORE(T, a, v)			/* nothing in this case */
#define load_reserved(T, a)         	h->expected_rd
#define store_conditional(T, a, v)  	h->expected_rd
#define cas32(a, b, c, d)   		h->expected_rd
#define cas64(a, b, c, d)   		h->expected_rd
#define amo_int32(a, b, c)  		h->expected_rd
#define amo_int64(a, b, c)  		h->expected_rd
#define riscv_syscall(a, b) 		h->expected_rd

#else
  
#define LOAD(T, a)			*(T*)(*ap++=a)
#define STORE(T, a, v)			*(T*)(*ap++=a)=(v)
#define load_reserved(T, a)		*(T*)(*ap++=a)
#define store_conditional(T, a, v)	wrd( (*(T*)(*ap++=a)=(v), 0) )
#define cas32(a, b, c, d)		cas<int32_t>(a, b, c, d)
#define cas64(a, b, c, d)		cas<int64_t>(a, b, c, d)
  // remaining have preexisting definitions

#endif
      
#define fence(x)
#define fence_i(x)
      
#define ebreak() kill(tid(), SIGTRAP)

      

  //#define stop debug.addval(s.reg[i->rd()].x); goto end_bb
  //#define stop        return pc
#define stop        goto jumped;

#define branch(test, taken, fall)  { if (test) { pc=(taken); stop; } else pc=(fall); }
#define jump(npc)  { pc=(npc); stop; }
#define reg_jump(npc)  { pc=(npc); stop; }
  
  switch (i->opcode()) {
  case Op_ZERO:	die("Should never see Op_ZERO at pc=%lx", pc);
#include "semantics.h"
  case Op_ILLEGAL:  die("Op_ILLEGAL opcode, i=%08x, pc=%lx", *(unsigned*)pc, pc);
  case Op_UNKNOWN:  die("Op_UNKNOWN opcode, i=%08x, pc=%lx", *(unsigned*)pc, pc);
  }
  //debug.addval(i->rd()!=NOREG ? s.reg[i->rd()].x : s.reg[i->rs2()].x);
  //return 0;
  pc = 0;			// no jump

 jumped:
  WRITE_REG(0, 0);
  return pc;
}
