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
Tcache_header_t* code;






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

#define ebreak()  die("breakpoint not implemented");
#define fence(n)

#define LOAD(typ, addr) *Load<typ>(*ap++=addr)
#define STORE(typ, addr, val) Store<typ>(*ap++=addr, val)

#define again  goto re_execute_instruction
#define stop  goto end_basic_block
#define jump(npc)  { pc=(npc); stop; }
#define branch(test, target, next)  { if (test) pc=(target); else { pc=(next); fall_thru=true; } stop; }
  
unordered_map<long, bb_header_t*> bbmap;
static bb_header_t* zero_link = 0;

void strand_t::interpreter(simfunc_t simulator, statfunc_t my_status)
{
  next_report = conf_report;
  bool fall_thru = false;	// set if conditional branch not taken
  bb_header_t** link = &zero_link;
  
  //  int target = 0;		// >0 means known target, points to first instruction
  //  bool taken = true;		// last conditional branch
  //  bb_footer_t* foot;
  
  long oldpc;
  for (;;) {			// once per basic block
    //    fprintf(stderr, "target=%d, pc=%lx\n", target, pc);
    bb_header_t* bb;
    bool newbb = false;
    if (*link && (*link)->addr == pc) // valid link from last basic block
      bb = *link;
    else { // no linkage or incorrect target (eg. jump register)
      std::unordered_map<long, bb_header_t*>::const_iterator pair = bbmap.find(pc);
      if (pair != bbmap.end()) { // existing target
	bb = pair->second;
	//	foot->link[taken] = (i - code.instruction);
	//	fprintf(stderr, "[%5ld] ", i-code.instruction);
	//	fprintf(stderr, "found target at pc %lx, linking [%ld][%d]=%d\n", (long)bb->addr, x, taken, foot->link[taken]);
      }
      else { // never seen target
	bb = code->new_basic_block(pc);
	bbmap[pc] = bb;		// hash table of pc->bb
	newbb = true;
	//	fprintf(stderr, "[%5ld] ", i-code.instruction);
	//	fprintf(stderr, "new basic block pc %lx\n", (long)bb->addr);
      }
      *link = bb;
    }
    long* ap = addresses;
    Insn_t* i = (Insn_t*)(bb+1);
    for(;; i++) {
      xrf[0] = 0;
      debug.insert(executed()+1, pc, i);
      oldpc = pc;
    re_execute_instruction:
      switch (i->opcode()) {
      case Op_ZERO:		// not yet decoded
	code->add_insn(decoder(pc)); again;
      case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
      case Op_UNKNOWN:  die("Op_UNKNOWN opcode");

#include "semantics.h"

      }
      debug.addval(i->rd()!=NOREG ? xrf[i->rd()] : xrf[i->rs2()]);
      //      print_trace(oldpc, i);
    }
    // at this point pc=target basic block but i still points to last instruction.
  end_basic_block:
    debug.addval(xrf[i->rd()]);
    //    print_trace(oldpc, i);
    if (newbb)
      code->end_basic_block((attributes[i->opcode()] & ATTR_cj) != 0);
    _executed += bb->count;
    if (_executed >= next_report) {
      status_report(my_status);
      next_report += conf_report;
    }
    simulator(hart, bb->addr, (Insn_t*)(bb+1), bb->count, addresses);
    // fall_thru==1 only if conditional branch not taken
    // i+1 points to branch-taken target; i+2 only if branch not taken
    link = (bb_header_t**)(i+1 + fall_thru);
    fall_thru = false;
  }
}

void strand_t::single_step()
{
  abort();
}

