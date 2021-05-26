/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#define DEBUG

#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "caveat.h"
#include "caveat_fp.h"
#include "arith.h"
#include "opcodes.h"
#include "insn.h"
//#include "shmfifo.h"
#include "cache.h"
#include "core.h"


#define trace_mem(code, a)  0
#define trace_bbk(code, v)
#define advance(sz)
#define restart()
#define update_regfile(rd, val)


//#define amoB(cpu, name, r1, r2)  amoBegin(cpu, name, r1, r2); fprintf(stderr, "%s%s(%s=%lx, %s=%ld)", color(cpu->tid), name, regName[r1], IR(r1).l, regName[r2], IR(r2).l)
//#define amoE(cpu, rd)            fprintf(stderr, "->%s=%ld\e[39m\n", regName[rd], IR(rd).l); amoEnd(cpu, rd)
#define amoB(cpu, name, r1, r2)    amoBegin(cpu, name, r1, r2)
#define amoE(cpu, rd)              amoEnd(cpu, rd)

int amolock;		   /* 0=unlock, 1=lock with no waiters, 2=lock with waiters */
unsigned long lrsc_set;  // globally shared location for atomic lock

#define MEM_ACTION(a)  0
#define JUMP_ACTION()

#ifndef DEBUG
#define indent(msg) fprintf(stderr, "%8ld%*s%s", num_insn, 2*(int)(cpu->csp-cpu->callstack+1), "", msg);
//#define show_call(ra, tgt) { indent("Call"); print_pc(tgt, stderr); fprintf(stderr, "<-"); print_pc(ra, stderr); fprintf(stderr, "\n"); print_callstack(cpu); }
//#define show_return(ra)    { indent("Return to"); print_pc(ra, stderr); fprintf(stderr, "\n"); }
#define show_call(ra, tgt) 
#define show_return(ra)    

#define PUSH_PC(ra, sub) { show_call(ra, tgt); cpu->debug.stack[cpu->debug.cs].tgt=sub; cpu->debug.stack[cpu->debug.cs].ra=ra; cpu->debug.cs=(cpu->debug.cs+1)&(MAXCALLDEPTH-1); }
#define POP_PC(ra)      { cpu->debug.cs=(cpu->debug.cs-1)&(MAXCALLDEPTH-1); }
#else
#define PUSH_PC(f, t)
#define POP_PC(ra)
#endif

/* Taken branches */
#define RETURN(npc, sz)  { POP_PC(npc); PC=npc; break; }
#define GOTO(npc, sz)    { PC=npc; break; }
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz;  PUSH_PC(PC+sz, npc); PC=tgt; break; }

#include "imacros.h"



void fast_sim(struct core_t* cpu, long istop)
{ 
#ifdef DEBUG
#define PC cpu->pc
#else
  Addr_t PC = cpu->pc;
#endif
  
  long icount = 0;
  while (cpu->exceptions == 0 && icount < istop) {
#ifdef DEBUG
    struct pctrace_t* t = &cpu->debug.trace[cpu->debug.tb];
    cpu->debug.tb = (cpu->debug.tb+1) & (PCTRACEBUFSZ-1);
    t->pc = PC;
#endif
    const struct insn_t* p = insn(PC);
    switch (p->op_code) {
#include "execute_insn.h"
    case Op_zero:
      fprintf(stderr, "Op_zero opcode\n");
      __sync_fetch_and_or(&cpu->exceptions, ILLEGAL_INSTRUCTION);
      break;
    case Op_illegal:
      fprintf(stderr, "Op_illegal opcode\n");
      __sync_fetch_and_or(&cpu->exceptions, ILLEGAL_INSTRUCTION);
      break;
    default:
      fprintf(stderr, "Unknown opcode\n");
      __sync_fetch_and_or(&cpu->exceptions, ILLEGAL_INSTRUCTION);
      break;
    }
    IR(0).l = 0L;		/* per ISA definition */
#ifdef DEBUG
    /* at this time p=just-executed instruction but PC=next */
    t->regval = cpu->reg[writeOp(p->op_code) ? p->op.rs2 : p->op_rd];
    if (cpu->conf.visible) {
      char buf[1024];
      char* b = buf;
      b += sprintf(b, "%s%ld ", color(cpu->tid), cpu->count.insn+icount);
      int rd = p->op_rd;
      if (p->op_rd != NOREG || writeOp(p->op_code))
	b += sprintf(b, "[%016lx]", t->regval.l);
      else
	b += sprintf(b, "[%16s]", "");
      b += format_pc(b, 29, t->pc);
      b += format_insn(b, insn(t->pc), t->pc, *((unsigned int*)t->pc));
      b += sprintf(b, "%s\n", nocolor);
      fputs(buf, stderr);
    }
#endif
    icount++;
  }
  cpu->pc = PC;		/* in case it is cached in register */
  cpu->count.insn += icount;
  cpu->count.cycle += icount;	/* fast_sim IPC=1.0 */
}
