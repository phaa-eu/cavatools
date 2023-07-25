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

#include "caveat.h"
#include "hart.h"

extern "C" {
#include "softfloat/softfloat.h"
#include "softfloat/softfloat_types.h"
#include "softfloat/specialize.h"
#include "softfloat/internals.h"
};

#include "arithmetic.h"

void substitute_cas(uintptr_t pc, Insn_t* i3);

inline float  m32(freg_t x) { union { freg_t r; float  f; } cv; cv.r=x; return cv.f; }
inline double m64(freg_t x) { union { freg_t r; double f; } cv; cv.r=x; return cv.f; }
inline float32_t n32(float  x)  { union { float32_t t; float  f; } cv; cv.f=x; return cv.t; }
inline float64_t n64(double x)  { union { float64_t t; double f; } cv; cv.f=x; return cv.t; }
    
//static Header_t mismatch_header = { 0, 0, 0, false, 0 };
static Header_t mismatch_header = Header_t(0, 0, 0, false);
static Header_t* mismatch = &mismatch_header;

#ifdef SPIKE
#include "spike_insns.h"
#endif

int hart_t::interpreter()
{
  uintptr_t addresses[1000];	// address list is one per strand
  try {
    Header_t** target = &mismatch;
    for (;;) {			// once per basic block
      Header_t* bb;		// current basic block
      if ((*target)->addr == pc) {
	bb = *target;		// valid link from last basic block
	//dbmsg("%8lx linked", pc);
      }
      else { // no linkage or incorrect target (eg. jump register)
	bb = tcache.find(pc);
	//if (bb) dbmsg("%8lx hit", pc);
	if (!bb) {	     // never seen target
	  
	  //pthread_mutex_lock(&tcache.lock);
	  
	  //
	  // Pre-decode entire basic block temporarily into address array
	  //
	  uintptr_t dpc = pc;	// decode pc
	  Header_t* wbb = (Header_t*)addresses;
	  Insn_t* j = (Insn_t*)(wbb+1) - 1; // note pre-incremented in loop
	  //dbmsg("Decoding");
	  do {
	    *++j = decoder(dpc);
	    
	    //labelpc(dpc);
	    //disasm(dpc, j);
	    
	    // instructions with attribute '<' must be first in basic block
	    if ((stop_before[j->opcode() / 64] >> (j->opcode() % 64) & 0x1L) && (j > insnp(wbb+1))) {
	      --j;		// remove ourself for next time
	      break;
	    }
	    dpc += j->compressed() ? 2 : 4;
	    // instructions with attribute '>' ends block
	    if (stop_after[j->opcode() / 64] >> (j->opcode() % 64) & 0x1L)
	      break;
	  } while (dpc-pc < 256 && j-insnp(wbb) < 128);
	  // pattern match store conditional if necessary
	  if (j->opcode()==Op_sc_w || j->opcode()==Op_sc_d)
	    substitute_cas(dpc-4, j);
	  //dbmsg("want to added bb %8lx count=%d", wbb->addr, wbb->count);
	  *wbb = Header_t(pc, dpc-pc, j+1-insnp(wbb+1), (attributes[j->opcode()] & ATTR_cj)!=0);
	  //
	  // Always end with one pointer to next basic block
	  // Conditional branches have second fall-thru pointer
	  //
	  *(Header_t**)(++j) = &mismatch_header; // space for branch taken pointer
	  if (wbb->conditional)
	    *(Header_t**)(++j) = &mismatch_header; // space for fall-thru pointer
	  
	  // atomically add block into tcache
	  long n = j - insnp(wbb) + 1;
	  bb = tcache.add(wbb, n);
	}
	*target = bb;
	//dbmsg("End decoding");
      }
      uintptr_t* ap = addresses;

      //
      // execute basic block
      //
      for (const Insn_t* i=insnp(bb+1); i<insnp(bb+1)+bb->count; i++) {
	_executed++;
	WRITE_REG(0, 0);
	debug.insert(pc, *i);
#if 0
	labelpc(pc);
	disasm(pc, i);
#endif
	/*
	  Abbreviations to keep isa.def semantics short
	*/
	
#ifdef SPIKE
	
	processor_t* p = &s.spike_cpu;

#undef STATE
#define STATE  (*s.spike_cpu.get_state())
#define xlen  64

#undef MMU
#define MMU  s.spike_mmu
	
#define wrd(e)	WRITE_REG(i->rd(), (e))
#define r1	READ_REG(i->rs1())
#define r2	READ_REG(i->rs2())
#define imm	i->immed()
#define wfd(e)	WRITE_FREG(i->rd()-FPREG, (e))
#define f1	READ_FREG(i->rs1()-FPREG)
#define f2	READ_FREG(i->rs2()-FPREG)
#define f3	READ_FREG(i->rs3()-FPREG)
 
#define w32(e)	WRITE_FREG(i->rd()-FPREG, freg(n32(e)))
#define w64(e)	WRITE_FREG(i->rd()-FPREG, freg(n64(e)))
	
#else
	
#define wrd(e)	s.xrf[i->rd()] = (e)
#define r1	s.xrf[i->rs1()]
#define r2	s.xrf[i->rs2()]
#define imm	i->immed()
#define wfd(e)	s.frf[i->rd()-FPREG] = freg(e)
#define f1	s.frf[i->rs1()-FPREG]
#define f2	s.frf[i->rs2()-FPREG]
#define f3	s.frf[i->rs3()-FPREG]

#define w32(e)	s.frf[i->rd()-FPREG] = freg(n32(e)) 
#define w64(e)	s.frf[i->rd()-FPREG] = freg(n64(e))
	
#endif

#define LOAD(T, a)     *(T*)(*ap++=a)
#define STORE(T, a, v) *(T*)(*ap++=a)=(v)
      
#define fence(x)
#define fence_i(x)
      
	//#define ebreak() return true
#define ebreak() kill(tid(), SIGTRAP)

#define stop debug.addval(s.xrf[i->rd()]); goto end_bb
#define spike_stop  target=&mismatch; stop

#define branch(test, taken, fall)  { if (test) { pc=(taken); target=(Header_t**)(i+1); } else { pc=(fall); target=(Header_t**)(i+2); } stop; }
#define jump(npc)  { pc=(npc); target=(Header_t**)(i+1); stop; }
#define reg_jump(npc)  { pc=(npc); target=&mismatch; stop; }
	//#define reg_jump(npc)  { pc=(npc); target=(Header_t**)(i+1); stop; }
	
	switch (i->opcode()) {
	case Op_ZERO:	die("Should never see Op_ZERO at pc=%lx", pc);
#include "semantics.h"
	case Op_ILLEGAL:  die("Op_ILLEGAL opcode, i=%08x, pc=%lx", *(unsigned*)pc, pc);
	case Op_UNKNOWN:  die("Op_UNKNOWN opcode, i=%08x, pc=%lx", *(unsigned*)pc, pc);
	}
	debug.addval(i->rd()!=NOREG ? s.xrf[i->rd()] : s.xrf[i->rs2()]);
      } // if loop exits there was no branch
      target = (Header_t**)(insnp(bb+1) + bb->count);
    end_bb:
      simulator(this, bb);
    }
  } catch (int retval) {
    return retval;
  }
}

  
void substitute_cas(uintptr_t pc, Insn_t* i3)
{
  dieif(i3->opcode()!=Op_sc_w && i3->opcode()!=Op_sc_d, "0x%lx no SC found in substitute_cas()", pc);

  Insn_t i2 = decoder(pc-4);
  if (i2.opcode() != Op_bne)
    i2 = decoder(pc-2);
  dieif(i2.opcode()!=Op_bne && i2.opcode()!=Op_c_bnez, "0x%lx instruction before SC not bne/bnez", pc);

  Insn_t i1 = decoder(pc-4 - (i2.compressed() ? 2 : 4));
  dieif(i1.opcode()!=Op_lr_w && i1.opcode()!=Op_lr_d, "0x%lx substitute_cas called without LR", pc);
  
  // pattern found, check registers
  int load_reg = i1.rd();
  int addr_reg = i3->rs1();
  int test_reg = (i2.opcode() == Op_c_bnez) ? 0 : i2.rs2();
  int newv_reg = i3->rs2();
  int flag_reg = i3->rd();
  dieif(i1.rs1()!=addr_reg || i2.rs1()!=load_reg, "0x%lx CAS pattern incorrect registers", pc);
  
  // pattern is good
  // note rd, rs1, rs2 stay the same
  i3->op_code = (i3->opcode()==Op_sc_w) ? Op_cas_w : Op_cas_d;
  i3->op.rs3 = test_reg;
}

