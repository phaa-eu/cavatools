/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define max(a, b)  ( a > b ? a : b )

{
  static long busy[256];	/* cycle when register becomes available */
  long pc =0;
  long icount =0;		/* instructions executed */
  long now =0;			/* current cycle */
  int cursor =0;		/* into mem_queue[] */

  uint64_t tr = fifo_get(in);
  for ( ;; ) {
    while (!is_frame(tr)) {
      if (is_mem(tr))
	mem_queue[cursor++] = tr;
      else if (is_bbk(tr)) {
	long epc = pc + tr_delta(tr);
	cursor = 0;			/* read list of memory addresses */
	while (pc < epc) {
	  long before_issue = now;
	  
	  /* model instruction fetch */
	  long pctag = pc & ib.tag_mask;
	  long blkidx = (pc >> ib.blksize) & ib.blk_mask;
	  if (pctag != ib.tag[ib.mru]) {
	    if (pctag == ib.tag[1-ib.mru]) { /* hit LRU: reverse MRU, LRU */
	      ib.mru = 1 - ib.mru;
	      ib.curblk = pc & ib.subblockmask;
	    }
	    else { /* miss: shift MRU to LRU, fill MRU */
	      ib.misses++;
#ifdef COUNT
	      *ibmiss(pc) += 1;
#endif
	      now += ib.penalty;
	      ib.mru = 1 - ib.mru;
	      ib.tag[ib.mru] = pctag;
	      memset(ib.ready[ib.mru], 0, ib.numblks*sizeof(long));
	      ib.curblk = pc & ib.subblockmask;
	      long ready = lookup_cache(&ic, pc, 0, now+ic.penalty);
	      ib.ready[ib.mru][ib.curblk] = ready;
	      if (ready == now+ic.penalty) {
#ifdef COUNT
		*icmiss(pc) += 1;
#endif
#ifdef TRACE
		fifo_put(out, trM(tr_i1get, pc));
#endif
	      }
	    }
	  }
	  now = max(now, ib.ready[ib.mru][blkidx]);
	  
	  /* scoreboarding: advance time until source registers not busy */
	  const struct insn_t* p = insn(pc);
	  now = max(now, busy[p->op_rs1]);
	  now = max(now, busy[p->op.rs2]);
	  if (threeOp(p->op_code))
	    now = max(now, busy[p->op.rs3]);
	
	  /* model loads and stores */
	  long ready = now;
	  if (memOp(p->op_code)) {
#ifdef TRACE
	    if (model_dcache)
	      ready = model_dcache(mem_queue[cursor++], p, now+dc.penalty);
	    else
#endif
	      ready = lookup_cache(&dc, tr_value(mem_queue[cursor++]), writeOp(p->op_code), now+dc.penalty);
	    /* note ready may be long in the past */
#ifdef COUNT
	    if (ready == now+dc.penalty)
	      *dcmiss(pc) += 1;
#endif
	  }
	  /* model function unit latency */
	  busy[p->op_rd] = ready + insnAttr[p->op_code].latency;
	  busy[NOREG] = 0;	/* in case p->op_rd not valid */
	  now += 1;		/* single issue machine */
#ifdef COUNT
	  struct count_t* c = count(pc);
	  c->count++;
	  c->cycles      += now - before_issue;
#endif
	  if (++icount >= next_report) {
	    status_report(now, icount);
	    next_report += report;
	  }
	  pc += shortOp(p->op_code) ? 2 : 4;
	} /* while (pc < epc) */
	if (is_goto(tr)) {	/* model taken branch */
	  pc = tr_pc(tr);
	  now += ib.delay;
	}
      } /* else if (is_bbk(tr)) */
      cursor = 0;	       /* get ready to enqueue another list */
      tr=fifo_get(in);
    } /* while (!is_frame(tr) */

    status_report(now, icount);
    if (tr_code(tr) == tr_eof)
      return;
    /* model discontinous trace segment */
    hart = tr_value(tr);
    pc = tr_pc(tr);
    ib.tag[0] = ib.tag[1] = 0L;	/* flush instruction buffer */
    flush_cache(&ic);		/* should we flush? */
    flush_cache(&dc);		/* should we flush? */
    tr=fifo_get(in);
  }
}
