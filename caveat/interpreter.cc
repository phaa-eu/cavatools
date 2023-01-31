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
#define branch(test, taken, fallthru)  { if (test) pc=(taken); else pc=(fallthru); stop; }
  

void strand_t::interpreter(simfunc_t simulator, statfunc_t my_status)
{
#ifdef DEBUG
  long oldpc;
#endif
  bb_header_t* bb = code.tcache->new_basic_block(pc);
  
  next_report = conf_report;
  for (;;) {			// once per basic block
    std::unordered_map<long, bb_header_t*>::const_iterator pair = code.umap.find(pc);
    bool newbb = pair == code.umap.end();
#if 0
    if (pair == code.umap.end()) { // never seen target
    }
    else {
    }
#endif
    
    bb_header_t* bb = (pair==code.umap.end()) ? code.tcache->new_basic_block(pc) : pair->second;
    Insn_t* i = (Insn_t*)bb + 1;
    long* ap = addresses;
    for(;; i++) {
      xrf[0] = 0;
#ifdef DEBUG
      debug.insert(executed()+1, pc, i);
      oldpc = pc;
#endif
    re_execute_instruction:
      switch (i->opcode()) {
      case Op_ZERO:		// not yet decoded
	code.tcache->add_insn(decoder(pc)); again;
      case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
      case Op_UNKNOWN:  die("Op_UNKNOWN opcode");

#include "semantics.h"

      }
#ifdef DEBUG
      debug.addval(i->rd()!=NOREG ? xrf[i->rd()] : xrf[i->rs2()]);
#endif
    }
  end_basic_block:
#ifdef DEBUG
    //    print_trace(oldpc, i);
    debug.addval(xrf[i->rd()]);
#endif
    if (newbb) {
      code.tcache->end_basic_block();
      code.umap[bb->addr]=bb;
    }
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

