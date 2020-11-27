/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define SAVE_STATS()  \
  stats.cycles = now; \
  stats.insns = insn_count; \
  stats.branches_taken = jump_count;

#define max(a, b)  a > b ? a : b

{
  static long busy[256];	/* cycle when register becomes available */
  long now =0;			/* current cycle */
  long insn_count =0;		/* instructions executed */
  long jump_count =0;		/* taken branches */
  int cursor =0;		/* into mem_queue[] */

  for (uint64_t tr=fifo_get(&trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(&trace_buffer)) { 
    if (is_mem(tr)) {
      mem_queue[cursor++] = tr;
      continue;
    }
    if (is_bbk(tr)) {
      long epc = pc + tr_delta(tr);
      cursor = 0;			/* read list of memory addresses */
      while (pc < epc) {
	const struct insn_t* p = insn(pc);
	/* scoreboarding: advance time until source registers not busy */
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
	if (visible && now-stall_begin > 0) {
	  fifo_put(&l2, trM(tr_stall, stall_begin));
	  fifo_put(&l2, trP(tr_issue, now-stall_begin, pc));
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
      if (is_goto(tr)) {
	pc = tr_pc(tr);
	++jump_count;
      }
      cursor = 0;	       /* get ready to enqueue another list */
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
      fifo_put(&l2, frame_header);
#endif
      continue;
    }
    if (tr_code(tr) == tr_icount) {
#ifdef SLOW
      fifo_put(&l2, trM(tr_icount, insn_count));
#endif
      continue;
    }

    
  }
  SAVE_STATS();
}
