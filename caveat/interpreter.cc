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
  next_report = conf_report;
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

