/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
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

extern "C" {
#include "softfloat/softfloat.h"
#include "softfloat/softfloat_types.h"
#include "softfloat/specialize.h"
#include "softfloat/internals.h"
};

#include "strand.h"
#include "arithmetic.h"

option<long> conf_report("report", 100000000, "Status report frequency");




Insn_t* tcache;			// Translated instructions and basic block info
//Tcache_header_t* code;






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


#define r1n0  i->rs1()!=0

#define wrd(e)	xrf[i->rd()] = (e)
#define r1	xrf[i->rs1()]
#define r2	xrf[i->rs2()]
#define imm	i->immed()

#define wfd(e)	frf[i->rd()-FPREG] = freg(e)
#define f1	frf[i->rs1()-FPREG]
#define f2	frf[i->rs2()-FPREG]
#define f3	frf[i->rs3()-FPREG]


#define LOAD(typ, addr) *Load<typ>(*ap++=addr)
#define STORE(typ, addr, val) Store<typ>(*ap++=addr, val)

#define again  goto re_execute_instruction

#define jump(npc)  { pc=(npc); link=(bb_header_t**)(i+1); goto end_basic_block; }
#define branch(test, target, next)  { if (test) { pc=(target); link=(bb_header_t**)(i+1); } \
    else { pc=(next); link=(bb_header_t**)(i+2); } goto end_basic_block; }

#define fence(n)  jump(pc+4)
#define ebreak()  die("breakpoint not implemented");

  
unordered_map<long, bb_header_t*> bbmap;
static bb_header_t* zero_link = 0;

Insn_t substitute_cas(long pc, Insn_t* i3)
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
  return Insn_t(i3->opcode()==Op_sc_w?Op_cas_w:Op_cas_d, flag_reg, addr_reg, newv_reg, test_reg, i3->immed());
}

void strand_t::interpreter(simfunc_t simulator, statfunc_t my_status)
{
  next_report = conf_report;
  Insn_t* end = tcache;
  bb_header_t** link = &zero_link;
  bb_header_t* bb;		// current basic block
  Insn_t* i;			// current instruction
  for (;;) {			// once per basic block
    if (*link && (*link)->addr == pc) // valid link from last basic block
      bb = *link;
    else { // no linkage or incorrect target (eg. jump register)
      std::unordered_map<long, bb_header_t*>::const_iterator pair = bbmap.find(pc);
      if (pair != bbmap.end())	// existing target
	bb = pair->second;
      else {			// never seen target
	//	fprintf(stderr, "new BB at %lx\n", pc);
	//	bb = code->new_basic_block(pc);
	bb = (bb_header_t*)end++;
	bb->addr = pc;
	bb->count = 0;
	bbmap[pc] = bb;		// hash table of pc->bb
	//
	// Pre-decode entire basic block
	//
	long dpc = pc;
	do {
	  i = end++;
	  *i = decoder(dpc);
	  dpc += i->compressed() ? 2 : 4;
	  bb->count++;
	} while (!(attributes[i->opcode()] & ATTR_stop));
	if (i->opcode()==Op_sc_w || i->opcode()==Op_sc_d)
	  *i = substitute_cas(dpc-4, i);
	//
	// Always end with one pointer to next basic block
	// Conditional branches have second fall-thru pointer
	//
	*(uint64_t*)end++ = 0;
	if (attributes[i->opcode()] & ATTR_cj)
	  *(uint64_t*)end++ = 0;
      }
      *link = bb;
    }
    long* ap = addresses;
    Insn_t* i = (Insn_t*)(bb+1);
    for(;; i++) {
      xrf[0] = 0;
      debug.insert(executed()+1, pc, i);
    re_execute_instruction:
      switch (i->opcode()) {
      case Op_ZERO:		// not yet decoded
	die("Should never see Op_ZERO");
	//	code->add_insn(decoder(pc)); again;
	*end++ = decoder(pc);
	*(uint64_t*)end = 0;
	again;
      case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
      case Op_UNKNOWN:  die("Op_UNKNOWN opcode");

#include "semantics.h"

      }
      debug.addval(i->rd()!=NOREG ? xrf[i->rd()] : xrf[i->rs2()]);
    }
    // at this point pc=target basic block but i still points to last instruction.
  end_basic_block:
    debug.addval(xrf[i->rd()]);
#if 0
    if (newbb) {
      //      code->end_basic_block((attributes[i->opcode()] & ATTR_cj) != 0);
      bb->count = end-(Insn_t*)bb;
      *(uint64_t*)end++=0;
      if (attributes[i->opcode()] & ATTR_cj)
	*(uint64_t*)end++=0;
      newbb = false;
    }
#endif
    _executed += bb->count;
    if (_executed >= next_report) {
      status_report(my_status);
      next_report += conf_report;
    }
    simulator(hart, bb->addr, (Insn_t*)(bb+1), bb->count, addresses);
  }
}

void strand_t::single_step()
{
  abort();
}

