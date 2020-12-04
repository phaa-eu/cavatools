/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define SAVE_STATS()  \
  stats.cycles = now; \
  stats.insns = insn_count; \
  stats.imisses = imisses;

#define max(a, b)  a > b ? a : b

{
  static long busy[256];	/* cycle when register becomes available */
  long now =0;			/* current cycle */
  long tag0=0, tag1=0;		/* instruction buffer tags */
  long ib_mask = ~((1<<lg_ib_line)-1);	/* ibuf line mask */
  long imisses=0;		/* number of instruction buffer misses */
  int cursor =0;		/* into mem_queue[] */
  long insn_count =0;		/* instructions executed */

  for (uint64_t tr=fifo_get(trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(trace_buffer)) { 
    if (is_mem(tr)) {
      mem_queue[cursor++] = tr;
      continue;
    }
    if (is_bbk(tr)) {
      long epc = pc + tr_delta(tr);
      cursor = 0;			/* read list of memory addresses */
      while (pc < epc) {
	/* model instruction fetch */

	long fpc = pc & ib_mask;  /* zero out low-order bits equal to buffer line size */
	if (fpc != tag0) {
	  if (fpc == tag1) {  /* reverse MRU, LRU tags */
	    long tmp = tag0;
	    tag0 = tag1;
	    tag1 = tmp;
	  }
	  else { /* miss, shift MRU to LRU, fill MRU */
	    tag1 = tag0;
	    tag0 = fpc;
	    imisses++;
#ifdef SLOW
	    fifo_put(l2, trM(tr_i1get, pc));
#endif
	    now += fetch_latency;
	  }
	}
	/* scoreboarding: advance time until source registers not busy */
	const struct insn_t* p = insn(pc);
#ifdef SLOW
	long stall_begin = now;
#endif
	now = max(now, busy[p->op_rs1]);
	now = max(now, busy[p->op.rs2]);
	if (threeOp(p->op_code))
	  now = max(now, busy[p->op.rs3]);
	/* model loads and stores */
	long ready = now;
	if (memOp(p->op_code)) {
#ifdef SLOW
	  if (model_dcache)
	    ready = model_dcache(mem_queue[cursor++], p, now+read_latency);
	  else
#endif
	    ready = lookup_cache(&dcache, tr_value(mem_queue[cursor++]), writeOp(p->op_code), now+read_latency);
	  /* note ready may be long in the past */
	}
	/* model function unit latency */
	busy[p->op_rd] = ready + insnAttr[p->op_code].latency;
	busy[NOREG] = 0;	/* in case p->op_rd not valid */
#ifdef SLOW
	if (timing && now-stall_begin > 0) {
	  fifo_put(l2, trM(tr_stall, stall_begin));
	  fifo_put(l2, trP(tr_issue, now-stall_begin, pc));
	}
#endif
	now += 1;		/* single issue machine */
	pc += shortOp(p->op_code) ? 2 : 4;
	if (++insn_count >= next_report) {
	  SAVE_STATS();
	  status_report(&stats);
	  next_report += report_frequency;
	}
      }
      if (is_goto(tr))
	pc = tr_pc(tr);
      cursor = 0;	       /* get ready to enqueue another list */
      continue;
    }
    if (tr_code(tr) == tr_icount) {
      //fprintf(stderr, "Got tr_icount=%ld, my icount=%ld, delta=%ld\n", tr_value(tr), insn_count, tr_value(tr)-insn_count);
#ifdef SLOW
      fifo_put(l2, trM(tr_icount, insn_count));
      fifo_put(l2, trM(tr_cycles, now));
#endif
      continue;
    }
    if (is_frame(tr)) {
      hart = tr_value(tr);
      pc = tr_pc(tr);
      flush_cache(&dcache);
      stats.segments++;
      SAVE_STATS()
      status_report(&stats);
#ifdef SLOW
      fifo_put(l2, frame_header);
#endif
      continue;
    }
  }
  SAVE_STATS();
}
