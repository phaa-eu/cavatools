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
#include <string.h>
#include <unordered_map>

#include "caveat.h"
#include "strand.h"

extern "C" {
#include "softfloat/softfloat.h"
#include "softfloat/softfloat_types.h"
#include "softfloat/specialize.h"
#include "softfloat/internals.h"
};

#include "arithmetic.h"

std::unordered_map<long, Header_t*> bbmap;

void substitute_cas(long pc, Insn_t* i3);

void strand_t::interpreter(simfunc_t simulator)
{
  Header_t* target = bbp(tcache); // tcache[0] never matches any pc
  for (;;) {			  // once per basic block
    Header_t* bb;		  // current basic block
    if (target->addr == pc) {
      bb = target;		// valid link from last basic block
    }
    else { // no linkage or incorrect target (eg. jump register)
      std::unordered_map<long, Header_t*>::const_iterator pair = bbmap.find(pc);
      if (pair != bbmap.end()) {
	bb = pair->second;	// existing target
      }
      else {			// never seen target
	//
	// Pre-decode entire basic block temporarily into address list
	//
	long dpc = pc;		// decode pc
	bb = bbp(addresses);
	Insn_t* j = insnp(addresses);
	do {
	  *++j = decoder(dpc);
	  dpc += j->compressed() ? 2 : 4;
	} while (!(attributes[j->opcode()] & ATTR_stop));
	if (j->opcode()==Op_sc_w || j->opcode()==Op_sc_d)
	  substitute_cas(dpc-4, j);
	bb->addr = pc;
	bb->count = j - insnp(addresses);
	//
	// Always end with one pointer to next basic block
	// Conditional branches have second fall-thru pointer
	//
	if (attributes[j->opcode()] & ATTR_cj)
	  *linkp(++j) = 0;	// space for 2nd pointer
	*linkp(++j) = 0;	// space for 1st pointer
	//
	// Atomically add basic block to tcache.
	//
	long n = j+1 - insnp(addresses);
	long oldlen = __sync_fetch_and_add(linkp(tcache+1), n);
	memcpy(tcache+oldlen, addresses, n*8);
	// reset bb to point into tcache
	bb = bbp(tcache) + oldlen;
	bbmap[bb->addr] = bb;	// this need to be atomic too!
      }
      *linkp(target) = insnp(bb) - tcache;
    }
    ap = addresses;
    Insn_t* i = insnp(bb+1);
    for(;; i++) {
      xrf[0] = 0;
      debug.insert(pc, i);
      // print_trace(pc, i);
      /*
	Abbreviations to keep isa.def semantics short
       */
#define wrd(e)	xrf[i->rd()] = (e)
#define r1	xrf[i->rs1()]
#define r2	xrf[i->rs2()]
#define imm	i->immed()
#define wfd(e)	frf[i->rd()-FPREG] = freg(e)
#define f1	frf[i->rs1()-FPREG]
#define f2	frf[i->rs2()-FPREG]
#define f3	frf[i->rs3()-FPREG]
#define branch(test, taken, fall)  { if (test) { pc=(taken); target=bbp(i+1); } else { pc=(fall); target=bbp(i+2); } goto end_bb; }
#define jump(npc)  { pc=(npc); target=bbp(i+1); goto end_bb; }
#define stop       { pc+=4;    target=bbp(i+1); goto end_bb; }
#define ebreak() die("ebreak not implemented yet")
#define fence(x)
#define fence_i(x)

      switch (i->opcode()) {
      case Op_ZERO:	die("Should never see Op_ZERO");
#include "semantics.h"
      case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
      case Op_UNKNOWN:  die("Op_UNKNOWN opcode");
      }
      debug.addval(i->rd()!=NOREG ? xrf[i->rd()] : xrf[i->rs2()]);
    }
  end_bb: // at this point pc=target basic block but i still points to last instruction.
    debug.addval(xrf[i->rd()]);
    simulator(hart_pointer, bb);
  }
}

void substitute_cas(long pc, Insn_t* i3)
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

void strand_t::single_step()
{
  abort();
}

