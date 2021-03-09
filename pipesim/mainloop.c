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

long histogram[4];

/*
Resources:
rs2	register port
alu	data path
mem	load/store port
br	branch unit
*/
#define r_rs2	(1L << __COUNTER__)
#define r_fs3	(1L << __COUNTER__)
#define r_alu	(1L << __COUNTER__)
#define r_mem	(1L << __COUNTER__)
#define r_fpu	(1L << __COUNTER__)
#define r_bru	(1L << __COUNTER__)
#define r_xwp	(1L << __COUNTER__)
#define r_fwp	(1L << __COUNTER__)


#define min(a, b)  ( a < b ? a : b )
#define max(a, b)  ( a > b ? a : b )

long iready[IBnumlines][IBnumblks]; /* time when subblock becomes available */
long itag[IBnumlines];		    /* pc tag of line */
long busy[256];		       /* cycle when register becomes valid */

static inline long dcache(long now, long addr, long pc, int store)
{
  /* note cache line time may be long in the past */
  long ready = max(now, lookup_cache(&sc, addr, store, now+sc.penalty));
#ifdef COUNT
  if (ready == now+sc.penalty)
    *dcmiss(pc) += 1;
#endif
#ifdef TRACE
  if (ready == now+sc.penalty) {
    if (*sc.evicted)
      fifo_put(out, trM(tr_d1put, *sc.evicted<<dc.lg_line));
    fifo_put(out, trM(tr_d1get, pc));
  }
#endif
  return ready;
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
      if (is_mem(tr))
	mem_queue[cursor++] = tr;
      else if (is_bbk(tr)) {
	long epc = pc + tr_delta(tr);
	cursor = 0;		/* read list of memory addresses */
	long before_issue = now; /* for counting stall cycles */
	while (pc < epc) {
	  long beginning = pc;
	  const struct insn_t* p = insn(pc);

	  /* fetch first instruction of bundle */
	  long pctag = pc >> IBlinesz2;
	  long blkidx = (pc>>IBblksz2) & IBblkmask;
	  /* hot path staying in same buffer line */
	  if (pctag != ctag) {
	    /* check all tags */
	    for (cidx=0; cidx<IBnumlines; cidx++) {
	      ctag = itag[cidx];
	      if (pctag == ctag)
		goto hit_exit;
	    }
	    /* no tags matched */
	    ibmisses++;
#ifdef COUNT
	    *ibmiss(pc) += 1;
#endif
	    /* replace next in fifo */
	    ififo = cidx = (ififo+1) & IBlinemask;
	    itag[cidx] = ctag = pctag;
	    /* fetch from shared cache */
	    /* note cache line time may be long in the past */
	    long when = max(now, lookup_cache(&sc, pc, 0, now+sc.penalty));
#ifdef COUNT
	    if (when == now+sc.penalty)
	      *dcmiss(pc) += 1;
#endif
#ifdef TRACE
	    if (when == now+sc.penalty) {
	      if (*sc.evicted)
		fifo_put(out, trM(tr_d1put, *sc.evicted<<dc.lg_line));
	      fifo_put(out, trM(tr_d1get, pc));
	    }
#endif
	    /* subblocks filled critical block first */
	    for (int k=0; k<IBnumblks; k++) {
	      iready[cidx][blkidx] = when++;
	      blkidx = (blkidx+1) & IBblkmask;
	    }
	  hit_exit: ;
	  } /* if (pctag != ctag) */
	  now = max(now, iready[cidx][blkidx]); /* iready may be in past */

	  /* scoreboarding: advance time until source registers not busy */
	  now = max(now, busy[p->op_rs1]);
	  now = max(now, busy[p->op.rs2]);
	  if (threeOp(p->op_code))
	    now = max(now, busy[p->op.rs3]);
	  /* model function unit latency */
	  busy[p->op_rd] = memOp(p->op_code)
	    ? dcache(now, tr_value(mem_queue[cursor++]), pc, writeOp(p->op_code))
	    : now+insnAttr[p->op_code].latency;
	  busy[NOREG] = 0;	/* in case p->op_rd not valid */
	  int consumed = insnAttr[p->op_code].flags;
	  long cutoff = min(pc+8, (pctag+1)<<IBlinesz2); /* bundle same cache line */
	  /* bookeeping */
#ifdef COUNT
	  struct count_t* c = count(pc);
	  c->count++;
	  c->cycles += now+1 - before_issue;
#endif
	  icount++;
	  pc += shortOp(p->op_code) ? 2 : 4;
	  int dispatched = 1;
	  /* dispatch up to 4 parcels in one cycle */
	  cutoff = min(cutoff, epc); /* stop after taken branch */
	  while (pc < cutoff) {
	    blkidx = (pc>>IBblksz2) & IBblkmask;
	    if (iready[cidx][blkidx] > now)
	      break;		/* subblock not ready */
	    /* resources available? */
	    p = insn(pc);
	    if (consumed & insnAttr[p->op_code].flags)
	      break;
	    /* scoreboarding:  end bundle if not ready */
	    if (busy[p->op_rs1] > now)
	      break;
	    if (busy[p->op.rs2] > now)
	      break;
	    if (threeOp(p->op_code) && busy[p->op.rs3] > now)
	      break;
	    /* model function unit latency */
	    busy[p->op_rd] = memOp(p->op_code)
	      ? dcache(now, tr_value(mem_queue[cursor++]), pc, writeOp(p->op_code))
	      : now+insnAttr[p->op_code].latency;
	    busy[NOREG] = 0;	/* in case p->op_rd not valid */
	    consumed |= insnAttr[p->op_code].flags;
	    /* bookeeping: note takes zero cycle */
#ifdef COUNT
	    count(pc)->count++;
#endif
	    icount++;
	    pc += shortOp(p->op_code) ? 2 : 4;
	    dispatched++;
	  } /* while (pc < cutoff) */
	  
	  /* sumarize issue */
	  histogram[dispatched]++;
	  *icmiss(beginning) += dispatched;
	  //fprintf(stderr, "now=%ld dispatched=%d npc=%lx\n", now, dispatched, pc);
	  now++;
	  before_issue = now;
	} /* while (pc < epc) */

	/* model taken branch */
	if (is_goto(tr)) {
	  pc = tr_pc(tr);
	  now += branch_delay;	/* charged to next instruction */
	}
      } /* else if (is_bbk(tr)) */
      cursor = 0;	       /* get ready to enqueue another list */
      tr=fifo_get(in);
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
    memset(itag, 0, sizeof itag);
    memset(iready, 0, sizeof iready);

    /* should we flush shared cache? */
    flush_cache(&sc);
    tr=fifo_get(in);
  } /* for (;;) */
}
