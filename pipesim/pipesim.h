/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


struct mru_cache_t {
  long tag;
  char dirty;
  long avail : 56;
};


struct statistics_t {
  long cycles;
  long insns;
  long segments;
  long imisses;
  struct timeval start_timeval;
};

extern struct statistics_t stats;
extern long frame_header;

extern struct cache_t dcache;
extern struct fifo_t* trace_buffer;
extern struct fifo_t* l2;
extern int hart;
extern uint64_t mem_queue[tr_memq_len];

extern int timing;
extern int quiet;

extern long fetch_latency;
extern long lg_ib_line;


extern long report_frequency;
void status_report(struct statistics_t* stats);

  
long dcache_writethru(long tr, const struct insn_t* p, long available);
long dcache_writeback(long tr, const struct insn_t* p, long available);

void fast_pipe(long pc, long read_latency, long next_report,
	       long (*model_dcache)(long tr, const struct insn_t* p, long available));
void slow_pipe(long pc, long read_latency, long next_report,
	       long (*model_dcache)(long tr, const struct insn_t* p, long available));

