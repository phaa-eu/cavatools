/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
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

#include "options.h"
#include "caveat.h"
#include "strand.h"

extern "C" {
#include "softfloat/softfloat.h"
#include "softfloat/softfloat_types.h"
#include "softfloat/specialize.h"
#include "softfloat/internals.h"
};

#include "arithmetic.h"

extern option<size_t> conf_tcache;
extern option<size_t> conf_hash;
extern option<bool> conf_show;

extern Tcache_t tcache;

void substitute_cas(uintptr_t pc, Insn_t* i3);

inline float  m32(freg_t x) { union { freg_t r; float  f; } cv; cv.r=x; return cv.f; }
inline double m64(freg_t x) { union { freg_t r; double f; } cv; cv.r=x; return cv.f; }
inline float32_t n32(float  x)  { union { float32_t t; float  f; } cv; cv.f=x; return cv.t; }
inline float64_t n64(double x)  { union { float64_t t; double f; } cv; cv.f=x; return cv.t; }
#define w32(e)	s.frf[i->rd()-FPREG] = freg(n32(e)) 
#define w64(e)	s.frf[i->rd()-FPREG] = freg(n64(e)) 
    
static Header_t mismatch_header = { 0, 0, 0, false, 0 };
static Header_t* mismatch = &mismatch_header;

	/*
	  Abbreviations to keep isa.def semantics short
	*/
#define wrd(e)	s.xrf[i->rd()] = (e)
#define r1	s.xrf[i->rs1()]
#define r2	s.xrf[i->rs2()]
#define imm	i->immed()
#define wfd(e)	s.frf[i->rd()-FPREG] = freg(e)
#define f1	s.frf[i->rs1()-FPREG]
#define f2	s.frf[i->rs2()-FPREG]
#define f3	s.frf[i->rs3()-FPREG]

#define LOAD(T, a)     *(T*)(*ap++=a)
#define STORE(T, a, v) *(T*)(*ap++=a)=(v)
      
#define fence(x)
#define fence_i(x)
  
#define branch(test, taken, fall)  { if (test) pc=(taken); else pc=(fall); goto end_bb; }
#define jump(npc)  { pc=(npc); goto end_bb; }
#define reg_jump(npc)  { pc=(npc); goto end_bb; }
#define stop       { pc+=4;    goto end_bb; }
#define ebreak() return true;

bool strand_t::single_step(bool show_trace)
{
  uintptr_t addresses[10];	// address list is one per strand
  Header_t* bb = const_cast<Header_t*>(tcache.bbptr(0));
  bb->addr = pc;
  bb->count = 1;
  Insn_t* i = (Insn_t*)bb + 2;	// skip over header
  uintptr_t oldpc = pc;
  *i = decoder(pc);
  uintptr_t* ap = addresses;
  if (i->opcode()==Op_sc_w || i->opcode()==Op_sc_d)
    substitute_cas(pc, i);
   
  s.xrf[0] = 0;
  debug.insert(pc, *i);
#if 0
  labelpc(pc);
  disasm(pc, i);
#endif 

  switch (i->opcode()) {
  case Op_ZERO:	die("Should never see Op_ZERO at pc=%lx", pc);
#include "semantics.h"
  case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
  case Op_UNKNOWN:  die("Op_UNKNOWN opcode");
  }
    
  debug.addval(i->rd()!=NOREG ? s.xrf[i->rd()] : s.xrf[i->rs2()]);
 end_bb: // at this point pc=target basic block but i still points to last instruction.
  debug.addval(s.xrf[i->rd()]);
  if (conf_show()) {
    print_trace(oldpc, i, stdout);
    //printf("%lx\n", pc);
  }
  hart_pointer->simulator(hart_pointer, 0);
  return false;
}
