/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
 */
#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <unistd.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <immintrin.h>

#include "caveat.h"
#include "caveat_fp.h"
#include "arith.h"
#include "opcodes.h"
#include "insn.h"
#include "cache.h"
#include "core.h"



static Addr_t cur_line;		/* instruction cache optimization */
/*
  Trace record creation
*/
static uint64_t hart;		/* multiplexed hart 0..15 << 4 */

//#define amoB(cpu, name, r1, r2)  amoBegin(cpu, name, r1, r2); fprintf(stderr, "%s%s(%s=%lx, %s=%ld)", color(cpu->tid), name, regName[r1], IR(r1).l, regName[r2], IR(r2).l)
//#define amoE(cpu, rd)            fprintf(stderr, "->%s=%ld\e[39m\n", regName[rd], IR(rd).l); amoEnd(cpu, rd)
#define amoB(cpu, name, r1, r2)    amoBegin(cpu, name, r1, r2)
#define amoE(cpu, rd)              amoEnd(cpu, rd)

static long last_event;		/* for delta in trace record */

#define MEM_ACTION(a)  VA=a
#define JUMP_ACTION()  consumed=~0

/* Taken branches */
#define RETURN(npc, sz)  { PC=npc; JUMP_ACTION(); break; }
#define GOTO(npc, sz)    { PC=npc; JUMP_ACTION(); break; }
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz; PC=tgt; JUMP_ACTION(); break; }


#include "imacros.h"


void slow_sim(core_t* cpu, long istop)
{
  //  fprintf(stderr, "slow_sim\n");
  cache_t* ic = &cpu->icache;
  cache_t* dc = &cpu->dcache;
  int corenum = cpu - core;
  struct count_t* countA = perf.count[corenum];
  long* icmissA = perf.icmiss[corenum];
  long* dcmissA = perf.dcmiss[corenum];
#define count(pc)  &countA[(pc-perf.h->base)/2]
#define icmiss(pc) &icmissA[(pc-perf.h->base)/2]
#define dcmiss(pc) &dcmissA[(pc-perf.h->base)/2]
  cpu->cur_line = ~0L;	/* current insn cache line set by ilookup() */
  
#define PC cpu->pc
  Addr_t VA;	      /* load/store address set by "execute_insn.h" */
  long now = cpu->count.cycle;
  //#define now cpu->count.cycle
  
  long icount = 0;
  while (cpu->exceptions == 0 && icount < istop) {
    /* calculate stall cycles before next bundle */
    long before_issue = now;
    /* model instruction cache with hot path */
    long addr = PC & ic->tag_mask;
    if (addr != cpu->cur_line) {
      int index = (addr & ic->row_mask) >> ic->lg_line;
      unsigned short* state = ic->states + index;
      struct lru_fsm_t* w = ic->fsm + *state; /* recall ic->fsm points to [-1] */
      struct lru_fsm_t* end = w + ic->ways;	 /* hence +ways = last entry */
      struct tag_t* tag;
      ic->refs++;
      do {
	w++;
	tag = ic->tags[w->way] + index;
	if (addr == tag->addr)
	  goto icache_hit;
      } while (w < end);
      ic->misses++;
      *icmiss(PC) += 1;
      //	trMiss(tr_fetch, addr, now);
      tag->addr = addr;
      tag->ready = now + ic->penalty;
      /* fifo_put(out, trM(tr_d1get, addr)); */
    icache_hit:
      *state = w->next_state;	/* already multiplied by ic->ways */
      cpu->cur_line = addr;		/* for hot path */
      if (tag->ready > now)
	now = tag->ready;
    }
    /* scoreboarding: advance time until source registers not busy */
    const struct insn_t* p = insn(PC);
    if (                        cpu->busy[p->op_rs1] > now) now = cpu->busy[p->op_rs1];
    if (!konstOp(p->op_code) && cpu->busy[p->op.rs2] > now) now = cpu->busy[p->op.rs2];
    if ( threeOp(p->op_code) && cpu->busy[p->op.rs3] > now) now = cpu->busy[p->op.rs3];
    /* stall charged to first instruction in bundle */
    struct count_t* c = count(PC);
    c->cycles += now - before_issue;
    /* issue superscalar bundle */
    int consumed = 0;		    /* resources already consumed */
    for (int dispatched=0; dispatched<3; dispatched++) {
#ifdef DEBUG
      struct pctrace_t* t = &cpu->debug.trace[cpu->debug.tb];
      cpu->debug.tb = (cpu->debug.tb+1) & (PCTRACEBUFSZ-1);
      t->pc = PC;
#endif
      /* interprete instruction */
      Addr_t lastPC = PC;
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
      if (conf.visible) {
	char buf[1024];
	char* b = buf;
	b += sprintf(b, "%sc%ld ", color(cpu->tid), now);
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
      if (cpu->exceptions)
	break;

      /* model data cache */
      long ready = now;
      if (memOp(p->op_code)) {
	addr = VA & dc->tag_mask; /* zero out byte-in-line bits */
	int index = (addr & dc->row_mask) >> dc->lg_line;
	unsigned short* state = dc->states + index;
	struct lru_fsm_t* w = dc->fsm + *state; /* recall dc->fsm points to [-1] */
	struct lru_fsm_t* end = w + dc->ways;	 /* hence +ways = last entry */
	struct tag_t* tag;
	dc->refs++;
	do {
	  w++;
	  tag = dc->tags[w->way] + index;
	  if (addr == tag->addr)
	    goto dcache_hit;
	} while (w < end);
	dc->misses++;
	*dcmiss(lastPC) += 1;
	/* evict old line and load new line */
#ifdef TRACE
	if (tag->dirty) {
	  trMiss(tr_update, tag->addr, now);
	  dc->evictions++;
	  tag->dirty = 0;
	}
	else
	  trMiss(tr_evict, tag->addr, now);
	if (writeOp(p->op_code))
	  trMiss(tr_exclusive, addr, now);
	else
	  trMiss(tr_shared, addr, now);
#else
	if (tag->dirty) {
	  dc->evictions++;
	  tag->dirty = 0;
	}
#endif
	tag->addr = addr;
	tag->ready = now + dc->penalty;
	tag->prefetch = 1;
      dcache_hit:
	*state = w->next_state;	/* already multiplied by dc->ways */
	if (writeOp(p->op_code)) {
#ifdef TRACE
	  if (!tag->dirty)
	    trMiss(tr_dirty, addr, now);
#endif
	  tag->dirty = 1;
	  dc->updates++;
	}
	if (tag->ready > ready)
	  ready = tag->ready;
#ifdef PREFETCH
	/* prefetch next line */
	if (tag->prefetch) {
	  tag->prefetch = 0;	/* only once */
	  addr += 1 << dc->lg_line;
	  index = (addr & dc->row_mask) >> dc->lg_line;
	  state = dc->states + index;
	  w = dc->fsm + *state;
	  end = w + dc->ways;
	  do {
	    w++;
	    tag = dc->tags[w->way] + index;
	    if (addr == tag->addr)
	      goto prefetch_hit;
	  } while (w < end);
#ifdef TRACE
	  if (tag->dirty) {
	    trMiss(tr_update, tag->addr, now);
	    dc->evictions++;
	    tag->dirty = 0;
	  }
	  else
	    trMiss(tr_evict, tag->addr, now);
	  if (writeOp(p->op_code))
	    trMiss(tr_exclusive, addr, now);
	  else
	    trMiss(tr_shared, addr, now);
#else
	  if (tag->dirty) {
	    dc->evictions++;
	    tag->dirty = 0;
	  }
#endif
	  tag->addr = addr;
	  tag->ready = now + dc->penalty + 1; /* XXX trailing edge */
	  tag->prefetch = 1;
	prefetch_hit:
	  *state = w->next_state;	/* already multiplied by dc->ways */
	}
#endif
      }
      /* model function unit latency for register scoreboarding */
      ready += insnAttr[p->op_code].latency;
      cpu->busy[p->op_rd] = ready;
      cpu->busy[NOREG] = 0;	/* in case p->op_rd not valid */
      /* model consumed resources */
      consumed |= insnAttr[p->op_code].flags;
      icount++;
      /* record superscalar-ness */
      c->count[dispatched]++;
      //      c->cycles++;
      /* advance to next instruction in bundle */
      c += shortOp(p->op_code) ? 1 : 2;
      p += shortOp(p->op_code) ? 1 : 2;
      /* end bundle if resource conflict */
      if ((consumed & insnAttr[p->op_code].flags) != 0)  break;
      /* register scoreboarding:  end bundle if not ready */
      if (                        cpu->busy[p->op_rs1] > now) break;
      if (!konstOp(p->op_code) && cpu->busy[p->op.rs2] > now) break;
      if ( threeOp(p->op_code) && cpu->busy[p->op.rs3] > now) break;
    } /* issued one superscalar bundle */
    now++;
  }
  cpu->count.insn += icount;
  cpu->count.cycle = now;
}
