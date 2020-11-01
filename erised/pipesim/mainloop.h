/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


{
  register long now =0;		/* current cycle */
  register long insn_count =0;	/* instructions executed */
  register long jump_count =0;	/* taken branches */
  register long mem_count =0;	/* memory references */
  register long write_count =0;	/* number of stores */
  static long busy[256];	/* cycle when register becomes available */
  register uint64_t* memq = mem_queue; /* help compiler allocate in register */
  register long cursor =0;	       /* into memq[] */

  for (uint64_t tr=fifo_get(&trace_buffer); tr_code(tr)!=tr_eof; tr=fifo_get(&trace_buffer)) {
    /* must come first, but rare and branch prediction will skip */
    if (tr_code(tr) == tr_start) {
      hart = tr_value(tr);
      pc = tr_pc(tr);
      flush_cache(&dcache);
      ++segments;
      status_report(now, insn_count, segments);
      continue;
    }
    if (tr_code(tr) == tr_icount) {
#ifdef L2CODE
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
      if (busy[p->op_rs1] > now)
	now = busy[p->op_rs1];
      if (busy[p->op.rs2] > now)
	now = busy[p->op.rs2];
      if (threeOp(p->op_code) && busy[p->op.rs3] > now)
	now = busy[p->op.rs3];
      now += 1;		     /* replace with table of FU's */
      /* model loads and stores */
      long when = now;
      if (memOp(p->op_code)) {
	mem_count++;
	long addr = tr_value(memq[cursor++]);
#ifdef L2CODE
	long tag = addr >> dcache.lg_line;
	if (writeOp(p->op_code)) {
	  write_count++;
#ifdef WRITETHRU
	  long sz = tr_size(memq[cursor-1]);
	  if (sz < 8) {	/* < 8B need L1 for ECC, 8B do not allocate */
	    if (lookup_Nway(&dcache, addr, 0, now+read_latency) == now+read_latency)
	      fifo_put(&l2, trM(tr_d1get, addr));
	  }
	  fifo_put(&l2, memq[cursor-1]);
#else
	  when = lookup_Nway(&dcache, addr, 1, now+read_latency);
#endif /* WRITETHRU */
	}
	else
	  when = lookup_Nway(&dcache, addr, 0, now+read_latency);
	if (when == now+read_latency) { /* cache miss */
	  if (dcache.evicted) {
	    fifo_put(&l2, trM(tr_d1put, dcache.evicted<<dcache.lg_line));
	  }
	  fifo_put(&l2, trM(tr_d1get, addr));
	}
#else /* L2CODE */
	when = lookup_Nway(&dcache, addr, writeOp(p->op_code), now+read_latency);
#endif /* L2CODE */
	when += insnAttr[p->op_code].latency;
      }
      busy[p->op_rd] = when;
      busy[NOREG] = 0;		/* in case p->op_rd not valid */
      pc += shortOp(p->op_code) ? 2 : 4;
      if (++insn_count >= next_report) {
	status_report(now, insn_count, segments);
	next_report += report_frequency;
      }
    }
    if (is_goto(tr)) {
      pc = tr_pc(tr);
      ++jump_count;
    }
    cursor = 0;			/* get ready to enqueue another list */
  }
  cycles_taken = now;
  insn_executed = insn_count;
  branches_taken = jump_count;
  memory_references = mem_count;
  store_insns = write_count;
}
