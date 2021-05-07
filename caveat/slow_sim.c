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


struct cache_t icache;		/* instruction cache model */
struct cache_t dcache;		/* data cache model */


static Addr_t cur_line;		/* instruction cache optimization */
/*
  Trace record creation
*/
static uint64_t hart;		/* multiplexed hart 0..15 << 4 */
static long last_event;		/* for delta in trace record */

static inline void trMiss(enum tr_opcode code, Addr_t addr, long now)
{
  if (now-last_event > 0xff) {
    fifo_put(trace, (now<<8) | hart | tr_time);
    last_event=now;
  }
  fifo_put(trace, (addr<<16) | ((now-last_event)<<8) | hart | (long)code);
  last_event = now;
}

long load_latency, fma_latency, branch_delay;

#define mpy_cycles   8
#define div_cycles  32
#define fma_div_cycles (fma_latency*3)


#define amo_lock_begin
#define amo_lock_end

#define INCPC(bytes)    PC+=bytes

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





/* Taken branches end superscalar bundle by consuming all resources */
#define RETURN(npc, sz)  { PC=npc; consumed=~0; break; }
#define JUMP(npc, sz)    { PC=npc; consumed=~0; break; }
#define GOTO(npc, sz)    { PC=npc; consumed=~0; break; }
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz; PC=tgt; consumed=~0; break; }

#define STATS(cpu) { cpu->pc=PC; cpu->counter.insn_executed+=cpu->params.report-icount; cpu->counter.cycles_simulated=now; }
#define UNSTATS(cpu) cpu->counter.insn_executed-=cpu->params.report-icount;

/* Special instructions, may exit simulation */
#define DOCSR(num, sz)  { STATS(cpu); proxy_csr(cpu, insn(PC), num); UNSTATS(cpu); }
#define ECALL(sz)       { STATS(cpu); if (proxy_ecall(cpu)) { cpu->state.mcause = 8; INCPC(sz); goto stop_slow_sim; } UNSTATS(cpu); }
#define EBRK(num, sz)   { STATS(cpu); cpu->state.mcause= 3; goto stop_slow_sim; } /* no INCPC! */



void only_sim(struct core_t* cpu)
#include "sim_body.h"

#define COUNT
void count_sim(struct core_t* cpu)
#include "sim_body.h"
#undef COUNT

#define TRACE
void trace_sim(struct core_t* cpu)
#include "sim_body.h"
#undef TRACE

#define TRACE
#define COUNT
void count_trace_sim(struct core_t* cpu)
#include "sim_body.h"
#undef TRACE
#undef COUNT
  
