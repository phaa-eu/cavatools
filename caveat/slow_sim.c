/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "caveat.h"
#include "caveat_fp.h"
#include "arith.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"
#include "cache.h"
#include "perfctr.h"
#include "pipesim.h"


struct ibuf_t  ib;		/* instruction buffer model */
struct cache_t ic;		/* instruction cache model */
struct cache_t dc;		/* data cache model */


#define advance(sz)  { since+=sz; if (since >= tr_max_number-4L) { fifo_put(cpu->tb, trP(tr_any, since, 0)); restart(); } }
#define restart()  (withregs ? dump_regs(cpu, updates) : 0, since=updates=0 )

#define amo_lock_begin
#define amo_lock_end


// Use only this macro to advance program counter
//#define INCPC(bytes)  { PC+=bytes; advance(bytes); }
#define INCPC(bytes)    PC+=bytes

// Discontinuous program counter macros
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz; INCPC(sz); PC=tgt; break; }
#define RETURN(npc, sz)  { Addr_t tgt=npc;                       INCPC(sz); PC=tgt; break; }
#define JUMP(npc, sz)    { Addr_t tgt=npc;                       INCPC(sz); PC=tgt; break; }
#define GOTO(npc, sz)    { Addr_t tgt=npc;                       INCPC(sz); PC=tgt; break; }

#define EBRK(num, sz)   { cpu->state.mcause= 3; goto stop_slow_sim; }
#define ECALL(sz)       if (proxy_ecall(cpu)) { cpu->state.mcause = 8; goto stop_slow_sim; }
#define DOCSR(num, sz)  proxy_csr(cpu, insn(PC), num)

// Memory reference instructions
#define LOAD_B( a, sz)  ( VA=a, *((          char*)(a)) )
#define LOAD_UB(a, sz)  ( VA=a, *((unsigned  char*)(a)) )
#define LOAD_H( a, sz)  ( VA=a, *((         short*)(a)) )
#define LOAD_UH(a, sz)  ( VA=a, *((unsigned short*)(a)) )
#define LOAD_W( a, sz)  ( VA=a, *((           int*)(a)) )
#define LOAD_UW(a, sz)  ( VA=a, *((unsigned   int*)(a)) )
#define LOAD_L( a, sz)  ( VA=a, *((          long*)(a)) )
#define LOAD_UL(a, sz)  ( VA=a, *((unsigned  long*)(a)) )
#define LOAD_F( a, sz)  ( VA=a, *((         float*)(a)) )
#define LOAD_D( a, sz)  ( VA=a, *((        double*)(a)) )

#define STORE_B(a, sz, v)  { VA=a, *((  char*)(a))=v; }
#define STORE_H(a, sz, v)  { VA=a, *(( short*)(a))=v; }
#define STORE_W(a, sz, v)  { VA=a, *((   int*)(a))=v; }
#define STORE_L(a, sz, v)  { VA=a, *((  long*)(a))=v; }
#define STORE_F(a, sz, v)  { VA=a, *(( float*)(a))=v; }
#define STORE_D(a, sz, v)  { VA=a, *((double*)(a))=v; }


// Define load reserve/store conditional emulation
#define addrW(rn)  (( int*)IR(rn).p)
#define addrL(rn)  ((long*)IR(rn).p)
#define amoW(rn)  ( VA=IR(rn).l, ( int*)IR(rn).p )
#define amoL(rn)  ( VA=IR(rn).l, (long*)IR(rn).p )

#define LR_W(rd, r1)      { amo_lock_begin; lrsc_set = IR(rd).ul&~0x7; IR(rd).l=*addrW(r1); VA=IR(r1).l; amo_lock_end; }
#define LR_L(rd, r1)      { amo_lock_begin; lrsc_set = IR(rd).ul&~0x7; IR(rd).l=*addrL(r1); VA=IR(r1).l; amo_lock_end; }
#define SC_W(rd, r1, r2)  { amo_lock_begin; if (lrsc_set == IR(r1).ul&~0x7) { *addrW(r1)=IR(r2).i; IR(rd).l=1; } else IR(rd).l=0;  VA=IR(r1).l; amo_lock_end; }
#define SC_L(rd, r1, r2)  { amo_lock_begin; if (lrsc_set == IR(r1).ul&~0x7) { *addrL(r1)=IR(r2).l; IR(rd).l=1; } else IR(rd).l=0;  VA=IR(r1).l; amo_lock_end; }


// Define AMO instructions
#define AMOSWAP_W(rd, r1, r2)  __sync_lock_test_and_set_4(amoW(r1), IR(r2).i)
#define AMOSWAP_L(rd, r1, r2)  __sync_lock_test_and_set_8(amoL(r1), IR(r2).l)

#define AMOADD_W(rd, r1, r2)   __sync_fetch_and_add_4( amoW(r1), IR(r2).i)
#define AMOADD_L(rd, r1, r2)   __sync_fetch_and_add_8( amoL(r1), IR(r2).l)
#define AMOXOR_W(rd, r1, r2)   __sync_fetch_and_xor_4( amoW(r1), IR(r2).i)
#define AMOXOR_L(rd, r1, r2)   __sync_fetch_and_xor_8( amoL(r1), IR(r2).l)
#define AMOOR_W( rd, r1, r2)   __sync_fetch_and_or_4(  amoW(r1), IR(r2).i)
#define AMOOR_L( rd, r1, r2)   __sync_fetch_and_or_8(  amoL(r1), IR(r2).l)
#define AMOAND_W(rd, r1, r2)   __sync_fetch_and_and_4( amoW(r1), IR(r2).i)
#define AMOAND_L(rd, r1, r2)   __sync_fetch_and_and_8( amoL(r1), IR(r2).l)

#define AMOMIN_W( rd, r1, r2) { amo_lock_begin;            int t1=*((          int*)amoW(r1)), t2=IR(r2).i;   if (t2 < t1) *addrW(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMAX_W( rd, r1, r2) { amo_lock_begin;            int t1=*((          int*)amoW(r1)), t2=IR(r2).ui;  if (t2 > t1) *addrW(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMIN_L( rd, r1, r2) { amo_lock_begin;           long t1=*((         long*)amoL(r1)), t2=IR(r2).i;   if (t2 < t1) *addrL(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMAX_L( rd, r1, r2) { amo_lock_begin;           long t1=*((         long*)amoL(r1)), t2=IR(r2).ui;  if (t2 > t1) *addrL(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMINU_W(rd, r1, r2) { amo_lock_begin;  unsigned  int t1=*((unsigned  int*)amoW(r1)), t2=IR(r2).l;   if (t2 < t1) *addrW(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMAXU_W(rd, r1, r2) { amo_lock_begin;  unsigned  int t1=*((unsigned  int*)amoW(r1)), t2=IR(r2).ul;  if (t2 > t1) *addrW(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMINU_L(rd, r1, r2) { amo_lock_begin;  unsigned long t1=*((unsigned long*)amoL(r1)), t2=IR(r2).l;   if (t2 < t1) *addrL(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }
#define AMOMAXU_L(rd, r1, r2) { amo_lock_begin;  unsigned long t1=*((unsigned long*)amoL(r1)), t2=IR(r2).ul;  if (t2 > t1) *addrL(r1) = t2;  IR(rd).l = t1;  amo_lock_end; }


// Define i-stream synchronization instruction
#define FENCE(rd, r1, immed)  {  __sync_synchronize();  INCPC(4); break; }


#define max(a, b) (a) > (b) ? (a) : (b)

static long busy[256];

void slow_sim(struct core_t* cpu, long report_frequency)
{
  register long countdown = report_frequency;
  register Addr_t PC = cpu->pc;
  register Addr_t VA;		/* load/store address */
  long now = cpu->counter.cycles_simulated;
  int since =0, updates=0;
  int withregs = (cpu->params.flags & tr_has_reg) != 0;
  while (1) {			/* exit by special opcodes above */
    do {
      const struct insn_t* p = insn(PC);
      Addr_t lastPC = PC;
      switch (p->op_code) {
#include "execute_insn.h"
      case Op_zero:
	abort();		/* should never occur */
      case Op_illegal:
	cpu->state.mcause = 2;	/* Illegal instruction */
	goto stop_slow_sim;
      default:
	cpu->state.mcause = 10; /* Unknown instruction */
	goto stop_slow_sim;
      }
      IR(0).l = 0L;
      long before_issue = now;
      /* scoreboarding: advance time until source registers not busy */
      now = max(now, busy[p->op_rs1]);
      now = max(now, busy[p->op.rs2]);
      if (threeOp(p->op_code))
	now = max(now, busy[p->op.rs3]);
      /* model loads and stores */
      long ready = now;
      if (memOp(p->op_code)) {
	ready = lookup_cache(&dc, VA, writeOp(p->op_code), now+dc.penalty);
	/* note ready may be long in the past */
	if (ready == now+dc.penalty)
	  *dcmiss(lastPC) += 1;
      }
      /* model function unit latency */
      busy[p->op_rd] = ready + insnAttr[p->op_code].latency;
      busy[NOREG] = 0;	/* in case p->op_rd not valid */
      now += 1;		/* single issue machine */
      struct count_t* c = count(lastPC);
      c->count++;
      c->cycles += now - before_issue;
    } while (--countdown > 0);
    cpu->pc = PC;  /* program counter cached in register */
    cpu->counter.insn_executed += report_frequency;
    cpu->counter.cycles_simulated = now;
    status_report(cpu, stderr);
    countdown = report_frequency;
  }
 stop_slow_sim:
  cpu->pc = PC;  /* program counter cached in register */
  cpu->counter.insn_executed += report_frequency - countdown;
  cpu->counter.cycles_simulated = now;
}

