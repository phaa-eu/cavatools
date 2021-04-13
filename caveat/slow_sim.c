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

static inline int dump_regs( struct core_t* cpu, int n)
{
  for (int i=0; i<n; i++)
    fifo_put(cpu->tb, regval[i]);
  return 0;
}

#define update_regfile(rd, val)  (withregs && (rd) != NOREG ? regval[updates++]=(val) : 0)
#define trace_mem(code, a)  fifo_put(cpu->tb, trM(code, a))
#define trace_bbk(code, v)  ( fifo_put(cpu->tb, trP(code, since, v)), restart() )
#define advance(sz)  { since+=sz; if (since >= tr_max_number-4L) { fifo_put(cpu->tb, trP(tr_any, since, 0)); restart(); } }
#define restart()  (withregs ? dump_regs(cpu, updates) : 0, since=updates=0 )
//#define on_every_insn(p)  if (cpu->params.verify) { fifo_put(&verify, cpu->holding_pc); cpu->holding_pc=PC; }
#define on_every_insn(p)

#define amo_lock_begin
#define amo_lock_end


// Use only this macro to advance program counter
#define INCPC(bytes)  { update_regfile(p->op_rd, IR(p->op_rd).l); PC+=bytes; advance(bytes); }

// Discontinuous program counter macros
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz; INCPC(sz); trace_bbk(tr_call,   tgt); PC=tgt; break; }
#define RETURN(npc, sz)  { Addr_t tgt=npc;                       INCPC(sz); trace_bbk(tr_return, tgt); PC=tgt; break; }
#define JUMP(npc, sz)    { Addr_t tgt=npc;                       INCPC(sz); trace_bbk(tr_jump,   tgt); PC=tgt; break; }
#define GOTO(npc, sz)    { Addr_t tgt=npc;                       INCPC(sz); trace_bbk(tr_branch, tgt); PC=tgt; break; }

#define EBRK(num, sz)   { cpu->state.mcause= 3; goto stop_slow_sim; }
#define ECALL(sz)       { if (proxy_ecall(cpu)) { cpu->state.mcause = 8; goto stop_slow_sim; } trace_bbk(tr_ecall, cpu->reg[17].l); }
#define DOCSR(num, sz)  { proxy_csr(cpu, insn(PC), num); trace_bbk(tr_csr, 0L);}

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


// Define load reserve/store conditional emulation
#define addrW(rn)  (( int*)IR(rn).p)
#define addrL(rn)  ((long*)IR(rn).p)
#define amoW(rn)  ( trace_mem(tr_amo4, IR(rn).l), ( int*)IR(rn).p )
#define amoL(rn)  ( trace_mem(tr_amo8, IR(rn).l), (long*)IR(rn).p )

#define LR_W(rd, r1)      { amo_lock_begin; lrsc_set = IR(rd).ul&~0x7; IR(rd).l=*addrW(r1); trace_mem(tr_lr4, IR(r1).ul|0x0L); amo_lock_end; }
#define LR_L(rd, r1)      { amo_lock_begin; lrsc_set = IR(rd).ul&~0x7; IR(rd).l=*addrL(r1); trace_mem(tr_lr8, IR(r1).ul|0x1L); amo_lock_end; }
#define SC_W(rd, r1, r2)  { amo_lock_begin; if (lrsc_set == IR(r1).ul&~0x7) { *addrW(r1)=IR(r2).i; IR(rd).l=1; } else IR(rd).l=0;  trace_mem(tr_sc4, IR(r1).ul|0x2L); amo_lock_end; }
#define SC_L(rd, r1, r2)  { amo_lock_begin; if (lrsc_set == IR(r1).ul&~0x7) { *addrL(r1)=IR(r2).l; IR(rd).l=1; } else IR(rd).l=0;  trace_mem(tr_sc8, IR(r1).ul|0x3L); amo_lock_end; }


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
#define FENCE(rd, r1, immed)  {  __sync_synchronize();  INCPC(4); trace_bbk(tr_fence, immed); break; }




void slow_sim(struct core_t* cpu, long report_frequency)
{
  register long countdown = report_frequency;
  register Addr_t PC = cpu->pc;
  int since =0, updates=0;
  int withregs = (cpu->params.flags & tr_has_reg) != 0;
  while (1) {			/* exit by special opcodes above */
    do {
      register const struct insn_t* p = insn(PC);
      on_every_insn(p);
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
    } while (--countdown > 0);
    cpu->pc = PC;  /* program counter cached in register */
    cpu->counter.insn_executed += report_frequency;
    status_report(cpu, stderr);
    trace_mem(tr_icount, cpu->counter.insn_executed);
    countdown = report_frequency;
  }
 stop_slow_sim:
  cpu->pc = PC;  /* program counter cached in register */
  cpu->counter.insn_executed += report_frequency - countdown;
  if (since > 0)
    trace_bbk(tr_any, 0);
}

