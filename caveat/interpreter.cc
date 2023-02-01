/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <unordered_map>

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

option<long> conf_tcache("tcache", 1024, "Binary translation cache size in 4K pages");

#undef RM
#define RM ({ int rm = i->immed(); \
              if(rm == 7) rm = fcsr.f.rm; \
              if(rm > 4) die("Illegal instruction"); \
              rm; })
#define srm  softfloat_roundingMode=RM
#define sfx  fcsr.f.flags |= softfloat_exceptionFlags;


#define r1n0  i->rs1()!=0

#define wrd(e)	xrf[i->rd()] = (e)
#define r1	xrf[i->rs1()]
#define r2	xrf[i->rs2()]
#define imm	i->immed()

#define wfd(e)	frf[i->rd()-FPREG] = freg(e)
#define f1	frf[i->rs1()-FPREG]
#define f2	frf[i->rs2()-FPREG]
#define f3	frf[i->rs3()-FPREG]

#define again  goto re_execute_instruction

#define jump(npc)  { pc=(npc); from=(bb_header_t*)(i+1); goto end_basic_block; }
#define branch(test, target, next)  { if (test) { pc=(target); from=(bb_header_t*)(i+1); } \
    else { pc=(next); from=(bb_header_t*)(i+2); } goto end_basic_block; }

#define fence(n)  jump(pc+4)
#define ebreak()  die("breakpoint not implemented");

  
std::unordered_map<long, bb_header_t*> bbmap;

void substitute_cas(long pc, Insn_t* i3);

void strand_t::interpreter(simfunc_t simulator)
{
  Insn_t* tcache = (Insn_t*)mmap(0, conf_tcache*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  long* addresses = (long*)mmap(0, 8*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  
  ((long*)tcache)[0] = 0;	// this bb_header_t never match any pc
  ((long*)tcache)[1] = 2;	// empty tcache
  bb_header_t* from = (bb_header_t*)tcache;

  for (;;) {			// once per basic block
    bb_header_t* bb;		// current basic block
    if (from->addr == pc) {
      bb = from;		// valid link from last basic block
      //fprintf(stderr, "Linked %lx +%d\n", (long)bb->addr, bb->count);
    }
    else { // no linkage or incorrect target (eg. jump register)
      std::unordered_map<long, bb_header_t*>::const_iterator pair = bbmap.find(pc);
      if (pair != bbmap.end()) {
	bb = pair->second;	// existing target
	//fprintf(stderr, "Found %lx +%d\n", (long)bb->addr, bb->count);
      }
      else {			// never seen target
	//
	// Pre-decode entire basic block temporarily into address list
	//
	long dpc = pc;		// decode pc
	bb = (bb_header_t*)addresses;
	Insn_t* j = (Insn_t*)addresses;
	do {
	  *++j = decoder(dpc);
	  //labelpc(dpc);
	  //disasm(dpc, j);
	  dpc += j->compressed() ? 2 : 4;
	} while (!(attributes[j->opcode()] & ATTR_stop));
	if (j->opcode()==Op_sc_w || j->opcode()==Op_sc_d)
	  substitute_cas(dpc-4, j);
	bb->addr = pc;
	bb->count = j - (Insn_t*)addresses;
	//
	// Always end with one pointer to next basic block
	// Conditional branches have second fall-thru pointer
	//
	if (attributes[j->opcode()] & ATTR_cj)
	  *(long*)++j = 0;
	*(long*)++j = 0;
	//
	// Atomically add basic block to tcache.
	//
	long n = j+1 - (Insn_t*)addresses;
	long oldlen = __sync_fetch_and_add((long*)(tcache+1), n);
	memcpy(tcache+oldlen, addresses, n*8);
	// reset bb to point into tcache
	bb = (bb_header_t*)tcache + oldlen;
	bbmap[bb->addr] = bb;
	//fprintf(stderr, "new BB[%ld] at %lx\n", bb-header, pc);
      }
      *(long*)from = (Insn_t*)bb - tcache;
    }
    ap = addresses;
    Insn_t* i = (Insn_t*)(bb+1);
    for(;; i++) {
      xrf[0] = 0;
      debug.insert(pc, i);
    re_execute_instruction:
      switch (i->opcode()) {
      case Op_ZERO:	die("Should never see Op_ZERO");
      case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
      case Op_UNKNOWN:  die("Op_UNKNOWN opcode");

#include "semantics.h"

      }
      debug.addval(i->rd()!=NOREG ? xrf[i->rd()] : xrf[i->rs2()]);
    }
    // at this point pc=target basic block but i still points to last instruction.
  end_basic_block:
    debug.addval(xrf[i->rd()]);
    simulator(hart(), bb->addr, (Insn_t*)(bb+1), bb->count, addresses);
  }
}

void substitute_cas(long pc, Insn_t* i3)
{
  //  fprintf(stderr, "substitute_cas(pc=%lx)\n", pc);
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

void strand_t::single_step()
{
  abort();
}

