/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "cache.h"
#include "perfctr.h"
#include "pipesim.h"

#define min(a, b)  ( a < b ? a : b )
#define max(a, b)  ( a > b ? a : b )

long busy[256];		       /* cycle when register becomes valid */

static inline long icache(long now, long addr, long pc)
{
  struct cache_t* c = &ic;
  c->refs++;
  addr >>= c->lg_line;		/* make proper tag (ok to include index) */
  int index = addr & c->row_mask;
  unsigned short* state = c->states + index;
  
  struct lru_fsm_t* p = c->fsm + *state; /* recall c->fsm points to [-1] */
  struct lru_fsm_t* end = p + c->ways;	 /* hence +ways = last entry */
  struct tag_t* tag;
  do {
    p++;
    tag = c->tags[p->way] + index;
    if (addr == tag->addr)
      goto cache_hit;
  } while (p < end);
  
  c->misses++;
  *icmiss(pc) += 1;
  tag->addr = addr;
  tag->ready = now+c->penalty;
#ifdef TRACE
  fifo_put(out, trM(tr_i1get, addr));
#endif
  
 cache_hit:
  *state = p->next_state;	/* already multiplied by c->ways */
  return tag->ready;
}


static inline long dcache(long now, long addr, int store, long pc)
{
  struct cache_t* c = &dc;
  c->refs++;
  addr >>= c->lg_line;		/* make proper tag (ok to include index) */
  int index = addr & c->row_mask;
  unsigned short* state = c->states + index;
  
  struct lru_fsm_t* p = c->fsm + *state; /* recall c->fsm points to [-1] */
  struct lru_fsm_t* end = p + c->ways;	 /* hence +ways = last entry */
  struct tag_t* tag;
  do {
    p++;
    tag = c->tags[p->way] + index;
    if (addr == tag->addr)
      goto cache_hit;
  } while (p < end);
  
  c->misses++;
  *dcmiss(pc) += 1;
  if (tag->dirty) {
#ifdef TRACE
    fifo_put(out, trM(tr_d1put, tag->addr<<c->lg_line));
#endif
    //    *c->evicted = tag->addr;	/* will SEGV if not cache not writable */
    c->evictions++;		/* can conveniently point to your location */
    tag->dirty = 0;
  }
  //  else if (c->evicted)
  //    *c->evicted = 0;
  tag->addr = addr;
  tag->ready = now+c->penalty;
#ifdef TRACE
  fifo_put(out, trM(tr_d1get, addr));
#endif
  
 cache_hit:
  *state = p->next_state;	/* already multiplied by c->ways */
  if (store) {
    tag->dirty = 1;
    c->updates++;
  }
  return tag->ready;
}


void simulate(long next_report)
{
  long pc =0;
  long icount =0;	 /* instructions executed */
  long ibmisses =0;	 /* instruction buffer misses */
  long now =0;		 /* current cycle */
  int cursor =0;	 /* into mem_queue[] */
  int ififo =0;		 /* oldest entry in fifo */
  int cidx =0;		 /* current ibuf line */
  long ctag =0;		 /* current tag = itag[cidx] */
  long report =0;

  uint64_t tr = fifo_get(in);
  for ( ;; ) {
    while (!is_frame(tr)) {
      while (is_mem(tr)) {
	mem_queue[cursor++] = tr_value(tr);
	tr = fifo_get(in);
      }
      if (is_bbk(tr)) {
	long epc = pc + tr_delta(tr);
	cursor = 0;		/* read list of memory addresses */
	long before_issue = now; /* for counting stall cycles */
	const struct insn_t* p = insn(pc);
	while (pc < epc) {
	  /* calculate stall cycles */
	  long ready = icache(now, pc, pc);
	  if (ready > now) now = ready;
	  /* scoreboarding: advance time until source registers not busy */
	  if (                        busy[p->op_rs1] > now) now = busy[p->op_rs1];
	  if (!konstOp(p->op_code) && busy[p->op.rs2] > now) now = busy[p->op.rs2];
	  if ( threeOp(p->op_code) && busy[p->op.rs3] > now) now = busy[p->op.rs3];
#ifdef COUNT
	  struct count_t* c = count(pc);
	  c->cycles += now - before_issue; /* stall charged to first instruction in bundle */
#endif
	  int dispatched = 0;
#if 0
	  long cutoff = min(pc+8, epc); /* stop after taken branch */
	  if ((pc^cutoff) & (1<<ic.lg_line))
	    cutoff &= ~((1<<ic.lg_line)-1); /* stop at end of cache line */
#endif
	  int consumed = 0;		    /* resources already consumed */
	  /* resource scorebording */
	  //while ((consumed & insnAttr[p->op_code].flags) == 0 && pc < cutoff) {
	  while ((consumed & insnAttr[p->op_code].flags) == 0) {
	    /* register scoreboarding:  end bundle if not ready */
	    if (                        busy[p->op_rs1] > now) break;
	    if (!konstOp(p->op_code) && busy[p->op.rs2] > now) break;
	    if ( threeOp(p->op_code) && busy[p->op.rs3] > now) break;
	    /* model function unit latency */
	    ready = now + insnAttr[p->op_code].latency;
	    if (memOp(p->op_code)) {
	      //long avail = dcache(now, tr_value(mem_queue[cursor++]), writeOp(p->op_code), pc);
	      long avail = dcache(now, mem_queue[cursor++], writeOp(p->op_code), pc);
	      if (avail > ready) ready = avail; /* avail may be long in the past */
	    }
	    busy[p->op_rd] = ready;
	    busy[NOREG] = 0;	/* in case p->op_rd not valid */
	    consumed |= insnAttr[p->op_code].flags;
	    icount++;
#ifdef COUNT
	    c->count[dispatched]++;
	    c->cycles++;
#endif
	    if (shortOp(p->op_code))
	      pc+=2, p+=1, c+=1;
	    else
	      pc+=4, p+=2, c+=2;
	    dispatched++;
	    //if (++dispatched >= 2) break;
	  }
	  /* sumarize dispatched bundle */
	  now++;
	  before_issue = now;
	} /* while (pc < epc) */

	/* model taken branch */
	if (is_goto(tr)) {
	  pc = tr_pc(tr);
	  now += branch_delay;	/* charged to next instruction */
	}
      } /* if (is_bbk(tr)) */
      else {
	if (!tr_code(tr) == tr_icount)
	  tr_print(tr, stderr);
      }
      tr=fifo_get(in);
      
      cursor = 0;	       /* get ready to enqueue another list */
      if (icount >= report) {
	status_report(now, icount, ibmisses);
	report += next_report;
      }
    } /* while (!is_frame(tr) */

    status_report(now, icount, ibmisses);
    if (tr_code(tr) == tr_eof)
      return;
    
    /* model discontinous trace segment */
    hart = tr_value(tr);
    pc = tr_pc(tr);
    /* should we flush caches? */
    flush_cache(&ic);
    flush_cache(&dc);
    tr=fifo_get(in);
  } /* for (;;) */
}
