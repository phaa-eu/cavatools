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

#include "caveat.h"
#include "hart.h"
#include "arithmetic.h"

#include "core.h"
	
#ifdef SPIKE
#error "Spike linkage not support at present"
#endif

void substitute_cas(uintptr_t pc, Insn_t* i3);

inline float  m32(freg_t x) { union { freg_t r; float  f; } cv; cv.r=x; return cv.f; }
inline double m64(freg_t x) { union { freg_t r; double f; } cv; cv.r=x; return cv.f; }
inline float32_t n32(float  x)  { union { float32_t t; float  f; } cv; cv.f=x; return cv.t; }
inline float64_t n64(double x)  { union { float64_t t; double f; } cv; cv.f=x; return cv.t; }

uintptr_t core_t::perform(Insn_t* i, uintptr_t pc)
{
  uintptr_t addrbuf[100];
  uintptr_t* ap = addrbuf;
  count_insn();
  WRITE_REG(0, 0);
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
	
#define LOAD(T, a)     *(T*)(*ap++=a)
#define STORE(T, a, v) *(T*)(*ap++=a)=(v)
      
#define fence(x)
#define fence_i(x)
      
  //#define ebreak() return true
#define ebreak() kill(tid(), SIGTRAP)
      

  //#define stop debug.addval(s.reg[i->rd()].x); goto end_bb
#define stop        return pc

#define branch(test, taken, fall)  { pc=(test)?(taken):(fall); stop; }
#define jump(npc)  { pc=(npc); stop; }
#define reg_jump(npc)  { pc=(npc); stop; }
  
  switch (i->opcode()) {
  case Op_ZERO:	die("Should never see Op_ZERO at pc=%lx", pc);
#include "semantics.h"
  case Op_ILLEGAL:  die("Op_ILLEGAL opcode, i=%08x, pc=%lx", *(unsigned*)pc, pc);
  case Op_UNKNOWN:  die("Op_UNKNOWN opcode, i=%08x, pc=%lx", *(unsigned*)pc, pc);
  }
  //debug.addval(i->rd()!=NOREG ? s.reg[i->rd()].x : s.reg[i->rs2()].x);
  return 0;
}
