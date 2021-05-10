/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

{
  struct cache_t* ic = &icache;
  struct cache_t* dc = &dcache;
  static long busy[256];	/* time when register available */
  long addr;
  Addr_t PC = cpu->pc;
  cur_line = ~0L;     /* current insn cache line set by ilookup() */
  Addr_t VA;	      /* load/store address set by "execute_insn.h" */
  long icount = cpu->params.report; /* instructions to be executed */
  long now = cpu->counter.cycles_simulated;
  while (cpu->state.mcause == 0) {
    /* calculate stall cycles before 1st of bundle in epoch */
    while (icount > 0) {
      /* calculate stall cycles before next bundle */
      long before_issue = now;
      /* model instruction cache with hot path */
      if ((addr = PC & ic->tag_mask) != cur_line) {
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
#ifdef COUNT
	*icmiss(PC) += 1;
#endif
#ifdef TRACE
	trMiss(tr_fetch, addr, now);
#endif
	tag->addr = addr;
	tag->ready = now + ic->penalty;
	/* fifo_put(out, trM(tr_d1get, addr)); */
      icache_hit:
	*state = w->next_state;	/* already multiplied by ic->ways */
	cur_line = addr;		/* for hot path */
	if (tag->ready > now)
	  now = tag->ready;
      }
      /* scoreboarding: advance time until source registers not busy */
      const struct insn_t* p = insn(PC);
      if (                        busy[p->op_rs1] > now) now = busy[p->op_rs1];
      if (!konstOp(p->op_code) && busy[p->op.rs2] > now) now = busy[p->op.rs2];
      if ( threeOp(p->op_code) && busy[p->op.rs3] > now) now = busy[p->op.rs3];
      /* stall charged to first instruction in bundle */
#ifdef COUNT
      struct count_t* c = count(PC);
      c->cycles += now - before_issue;
#endif
      /* issue superscalar bundle */
      int consumed = 0;		    /* resources already consumed */
      for (int dispatched=0; ; dispatched++) {
#ifdef DEBUG
	fprintf(stderr, "%d ", dispatched);
	print_pc(PC, stderr);
	print_insn(PC, stderr);
#endif
	/* interprete instruction */
	Addr_t lastPC = PC;
	switch (p->op_code) {
#include "execute_insn.h"
	case Op_zero:
	  abort();		/* should never occur */
	case Op_illegal:
	  cpu->state.mcause = 2;	/* Illegal instruction */
	  goto stop_run;
	default:
	  cpu->state.mcause = 10; /* Unknown instruction */
	  goto stop_run;
	}
	IR(0).l = 0L;
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
#ifdef COUNT
	  *dcmiss(PC) += 1;
#endif
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
	}
	/* model function unit latency for register scoreboarding */
	ready += insnAttr[p->op_code].latency;
	busy[p->op_rd] = ready;
	busy[NOREG] = 0;	/* in case p->op_rd not valid */
	/* model consumed resources */
	consumed |= insnAttr[p->op_code].flags;
	icount--;
	/* advance to next instruction in bundle */
	p += shortOp(p->op_code) ? 1 : 2;
	/* record superscalar-ness */
#ifdef COUNT
	c->count[dispatched]++;
	c->cycles++;
	c += shortOp(p->op_code) ? 1 : 2;
#endif
	/* end bundle if resource conflict */
	if ((consumed & insnAttr[p->op_code].flags) != 0)  break;
	/* register scoreboarding:  end bundle if not ready */
	if (                        busy[p->op_rs1] > now) break;
	if (!konstOp(p->op_code) && busy[p->op.rs2] > now) break;
	if ( threeOp(p->op_code) && busy[p->op.rs3] > now) break;
      } /* issued one superscalar bundle */
      now++;
    }
    STATS(cpu);
    status_report(cpu, stderr);
    icount = cpu->params.report;
  }
 stop_run:
  status_report(cpu, stderr);
}

