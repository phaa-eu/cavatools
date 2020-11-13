/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define SAVE_STATS()  \
  stats.cycles = now; \
  stats.instructions = insn_count; \
  stats.branches_taken = jump_count; \
  stats.mem_refs = mem_count; \
  stats.stores = write_count;

#define max(a, b)  a > b ? a : b

{
  register long now =0;		/* current cycle */
  register long insn_count =0;	/* instructions executed */
  register long jump_count =0;	/* taken branches */
  register long mem_count =0;	/* memory references */
  register long write_count =0;	/* number of stores */

  static long busy[256];	/* cycle when register becomes available */
  extern uint64_t mem_queue[];
  register uint64_t* memq = mem_queue; /* help compiler allocate in register */
  register long cursor =0;	       /* into memq[] */
  
  extern struct fifo_t trace_buffer;
  extern int hart;

  for (uint64_t tr=fifo_get(&trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(&trace_buffer)) {
    /* must come first, but rare and branch prediction will skip */
    if (tr_code(tr) == tr_start) {
      hart = tr_value(tr);
      pc = tr_pc(tr);
      flush_cache(&dcache);
      stats.segments++;
      SAVE_STATS()
      status_report(&stats);
      continue;
    }
    if (tr_code(tr) == tr_icount) {
#ifdef SLOW
      fifo_put(&l2, trM(tr_icount, insn_count));
#endif
      continue;
    }
    if (is_mem(tr)) {
      memq[cursor++] = tr;
      continue;
    }
    register long epc = pc + tr_number(tr);
    cursor = 0;			/* read list of memory addresses */
    while (pc < epc) {
      register const struct insn_t* p = insn(pc);
      /* scoreboarding: advance time until source registers not busy */
      now = max(now, busy[p->op_rs1]);
      now = max(now, busy[p->op.rs2]);
      now = max(now, busy[p->op.rs3]);
      /* model loads and stores */
      long when = now;
      if (memOp(p->op_code)) {
	mem_count++;
	if (writeOp(p->op_code))
	  write_count++;
#ifdef SLOW
	when = model_dcache(memq[cursor++], p, now+read_latency);
#else
	when = lookup_Nway(&dcache, tr_value(memq[cursor++]), writeOp(p->op_code), now+read_latency);
#endif
	when += insnAttr[p->op_code].latency;
      }
      busy[p->op_rd] = when;
      busy[NOREG] = 0;		/* in case p->op_rd not valid */
#ifdef SLOW
      if (visible) {
	//	if (begin <= pc && pc <= end)
	issue_insn(pc, p, now);
      }
#endif
      now += 1;  /* single issue machine */
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
    cursor = 0;			/* get ready to enqueue another list */
  }
  SAVE_STATS();
}
