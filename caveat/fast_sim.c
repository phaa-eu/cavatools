/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
//#define DEBUG
#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "caveat.h"
#include "caveat_fp.h"
#include "arith.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"


#define trace_mem(code, a)  0
#define trace_bbk(code, v)
#define advance(sz)
#define restart()
#define update_regfile(rd, val)



/* Use only this macro to advance program counter */
#define INCPC(bytes)	PC+=bytes


// Memory reference instructions
#define LOAD_B( a, sz)  ( trace_mem(tr_read1, a), *((          char*)(a)) )
#define LOAD_UB(a, sz)  ( trace_mem(tr_read1, a), *((unsigned  char*)(a)) )
#define LOAD_H( a, sz)  ( trace_mem(tr_read2, a), *((         short*)(a)) )
#define LOAD_UH(a, sz)  ( trace_mem(tr_read2, a), *((unsigned short*)(a)) )
#define LOAD_W( a, sz)  ( trace_mem(tr_read4, a), *((           int*)(a)) )
#define LOAD_UW(a, sz)  ( trace_mem(tr_read4, a), *((unsigned   int*)(a)) )
#define LOAD_L( a, sz)  ( trace_mem(tr_read8, a), *((          long*)(a)) )
#define LOAD_UL(a, sz)  ( trace_mem(tr_read8, a), *((unsigned  long*)(a)) )
#define LOAD_F( a, sz)  ( trace_mem(tr_read4, a), *((         float*)(a)) )
#define LOAD_D( a, sz)  ( trace_mem(tr_read8, a), *((        double*)(a)) )

#define STORE_B(a, sz, v)  { trace_mem(tr_write1, a); *((  char*)(a))=v; }
#define STORE_H(a, sz, v)  { trace_mem(tr_write2, a); *(( short*)(a))=v; }
#define STORE_W(a, sz, v)  { trace_mem(tr_write4, a); *((   int*)(a))=v; }
#define STORE_L(a, sz, v)  { trace_mem(tr_write8, a); *((  long*)(a))=v; }
#define STORE_F(a, sz, v)  { trace_mem(tr_write4, a); *(( float*)(a))=v; }
#define STORE_D(a, sz, v)  { trace_mem(tr_write8, a); *((double*)(a))=v; }



pthread_mutex_t amolock;

static const char* color[] =
  {
   [0] = "\e[31m",		/* Red */
   [1] = "\e[32m",		/* Green */
   [2] = "\e[33m",		/* Yellow */
   [3] = "\e[34m",		/* Blue */
   [4] = "\e[35m",		/* Magenta */
   [5] = "\e[36m",		/* Cyan */
   [6] = "\e[37m",		/* Light Gray */
   [7] = "\e[97m",		/* White */
  };

static void amoBegin(struct core_t* cpu, const char* name, int r1, int r2)
{
  pid_t tid = syscall(SYS_gettid);
  fprintf(stderr, "%s%s(%s=%lx, %s=%lx)", color[tid%8], name, regName[r1], IR(r1).l, regName[r2], IR(r2).l);
  pthread_mutex_lock(&amolock);
}

static void amoEnd(struct core_t* cpu, int rd)
{
  pthread_mutex_unlock(&amolock);
  fprintf(stderr, "->%s=%lx\e[39m\n", regName[rd], IR(rd).l);
}

//#define amoB(cpu, name, r1, r2) amoBegin(cpu, name, r1, r2)
//#define amoE(cpu, rd)           amoEnd(cpu, rd)
#define amoB(cpu, name, r1, r2)
#define amoE(cpu, rd)          


// Define load reserve/store conditional emulation
#define addrW(rn)  (( int*)IR(rn).p)
#define addrL(rn)  ((long*)IR(rn).p)

#define LR_W(rd, r1)          { amoB(cpu, "LR_W",      r1,  0); lrsc_set=IR(r1).l; IR(rd).l=*addrW(r1); amoE(cpu, rd); }
#define LR_L(rd, r1)          { amoB(cpu, "LR_L",      r1,  0); lrsc_set=IR(r1).l; IR(rd).l=*addrL(r1); amoE(cpu, rd); }
#define SC_W(rd, r1, r2)      { amoB(cpu, "SC_W",      r1, r2); IR(rd).l=(lrsc_set==IR(r1).l) ? (*addrW(r1)=IR(r2).l, 0) : 1; amoE(cpu, rd); }
#define SC_L(rd, r1, r2)      { amoB(cpu, "SC_W",      r1, r2); IR(rd).l=(lrsc_set==IR(r1).l) ? (*addrL(r1)=IR(r2).l, 0) : 1; amoE(cpu, rd); }
#define FENCE(rd, r1, immed)  { amoB(cpu, "FENCE",     r1,  0); amoE(cpu, rd); }

// Define AMO instructions
#define AMOSWAP_W(rd, r1, r2) { amoB(cpu, "AMOSWAP_W", r1, r2); int  t=*addrW(r1); *addrW(r1) =IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOADD_W(rd, r1, r2)  { amoB(cpu, "AMOADD_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)+=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOXOR_W(rd, r1, r2)  { amoB(cpu, "AMOXOR_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)^=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOOR_W( rd, r1, r2)  { amoB(cpu, "AMOOR_W",   r1, r2); int  t=*addrW(r1); *addrW(r1)|=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOAND_W(rd, r1, r2)  { amoB(cpu, "AMOAND_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)&=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }

#define AMOSWAP_L(rd, r1, r2) { amoB(cpu, "AMOSWAP_L", r1, r2); long t=*addrL(r1); *addrL(r1) =IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOADD_L(rd, r1, r2)  { amoB(cpu, "AMOADD_L",  r1, r2); long t=*addrL(r1); *addrL(r1)+=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOXOR_L(rd, r1, r2)  { amoB(cpu, "AMOXOR_L",  r1, r2); long t=*addrL(r1); *addrL(r1)^=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOOR_L( rd, r1, r2)  { amoB(cpu, "AMOOR_L",   r1, r2); long t=*addrL(r1); *addrL(r1)|=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOAND_L(rd, r1, r2)  { amoB(cpu, "AMOAND_L",  r1, r2); long t=*addrL(r1); *addrL(r1)&=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }

#define AMOMIN_W( rd, r1, r2) { amoB(cpu, "AMOMIN_W",  r1, r2);           int t1=*((          int*)addrW(r1)), t2=IR(r2).i;  if (t2 < t1) *addrW(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAX_W( rd, r1, r2) { amoB(cpu, "AMOMAX_W",  r1, r2);           int t1=*((          int*)addrW(r1)), t2=IR(r2).i;  if (t2 > t1) *addrW(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMINU_W(rd, r1, r2) { amoB(cpu, "AMOMINU_W", r1, r2); unsigned  int t1=*((unsigned  int*)addrW(r1)), t2=IR(r2).ui; if (t2 < t1) *addrW(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }
#define AMOMAXU_W(rd, r1, r2) { amoB(cpu, "AMOMAXU_W", r1, r2); unsigned  int t1=*((unsigned  int*)addrW(r1)), t2=IR(r2).ui; if (t2 > t1) *addrW(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }

#define AMOMIN_L( rd, r1, r2) { amoB(cpu, "AMOMIN_L",  r1, r2);          long t1=*((         long*)addrL(r1)), t2=IR(r2).l;  if (t2 < t1) *addrL(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAX_L( rd, r1, r2) { amoB(cpu, "AMOMAX_L",  r1, r2);          long t1=*((         long*)addrL(r1)), t2=IR(r2).l;  if (t2 > t1) *addrL(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMINU_L(rd, r1, r2) { amoB(cpu, "AMOMINU_L", r1, r2); unsigned long t1=*((unsigned long*)addrL(r1)), t2=IR(r2).ul; if (t2 < t1) *addrL(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }
#define AMOMAXU_L(rd, r1, r2) { amoB(cpu, "AMOMAXU_L", r1, r2); unsigned long t1=*((unsigned long*)addrL(r1)), t2=IR(r2).ul; if (t2 > t1) *addrL(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }




/* Taken branches end superscalar bundle by consuming all resources */
#define RETURN(npc, sz)  { PC=npc; break; }
#define JUMP(npc, sz)    { PC=npc; break; }
#define GOTO(npc, sz)    { PC=npc; break; }
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz; PC=tgt; break; }

#define STATS(cpu) { cpu->pc=PC; cpu->counter.insn_executed+=cpu->params.report-icount; }
#define UNSTATS(cpu) cpu->counter.insn_executed-=cpu->params.report-icount;


/* Special instructions, may exit simulation */

int ecall_wrapper(struct core_t* cpu)
{
  int rc = proxy_ecall(cpu);
  //  pid_t tid = syscall(SYS_gettid);
  //  fprintf(stderr, "ecall_wrapper, pid=%d, tid=%d\n", getpid(), tid);
  return rc;
}

#define DOCSR(num, sz)  STATS(cpu); proxy_csr(cpu, insn(PC), num); UNSTATS(cpu);
#define ECALL(sz)       STATS(cpu); if (ecall_wrapper(cpu)) { cpu->state.mcause = 8; INCPC(sz); goto stop_fast_sim; } UNSTATS(cpu);
#define EBRK(num, sz)   STATS(cpu); cpu->state.mcause= 3; goto stop_fast_sim; /* no INCPC! */





void fast_sim(struct core_t* cpu)
{
  pid_t tid = syscall(SYS_gettid);
  Addr_t PC = cpu->pc;
  long icount = cpu->params.report;
  while (1) {			/* exit by special opcodes above */
    while (icount > 0) {
      //#ifdef DEBUG
      if (cpu->params.visible) {
	fprintf(stderr, "%s %x", color[tid%8], tid);
	fprintf(stderr, "F ");
	print_pc(PC, stderr);
	print_insn(PC, stderr);
	fprintf(stderr, "\e[39m");
      }
      //#endif
      const struct insn_t* p = insn(PC);
      switch (p->op_code) {
#include "execute_insn.h"
      case Op_zero:
	fprintf(stderr, "ZERO opcode at %lx\n", PC);
	abort();		/* should never occur */
      case Op_illegal:
	cpu->state.mcause = 2;	/* Illegal instruction */
	goto stop_fast_sim;
      default:
	cpu->state.mcause = 10; /* Unknown instruction */
	goto stop_fast_sim;
      }
      IR(0).l = 0L;
      icount--;
    }
    STATS(cpu);
    status_report(cpu, stderr);
    icount = cpu->params.report;
  }

 stop_fast_sim:
  status_report(cpu, stderr);
}





void single_step(struct core_t* cpu)
{
  long icount = 1;
#define PC cpu->pc
  register const struct insn_t* p = insn(PC);
  switch (p->op_code) {
#include "execute_insn.h"
  case Op_zero:
    abort();		/* should never occur */
  case Op_illegal:
    cpu->state.mcause = 2;	/* Illegal instruction */
    break;
  default:
    cpu->state.mcause = 10; /* Unknown instruction */
    break;
  }
 stop_fast_sim:
  IR(0).l = 0L;
  STATS(cpu);
}
