/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

struct count_t {		/* CPI = cycles/count */
  struct insn_t i;		/* decoded instruction */
  long count;			/* how many times executed */
  long cycles;			/* total including stalls */
};

struct countSpace_t {
  Addr_t base, bound;
  struct count_t* insn_array;
};


struct ibuf_t {
  long tag[2];
  long* ready[2];	/* ready[2][numblks] */
  long tag_mask;	/* pc mask = (1 << lg_line) - 1 */
  long blk_mask;	/* block index mask = numblks - 1 */
  long misses;
  int lg_line;		/* log-base-2 of line size in bytes */
  int lg_blksize;	/* log-base-2 of block size in bytes */
  int numblks;		/* = (1<<lg_line)/(1<<lg_blksize) */
  int penalty;		/* number of cycles to refill critical block */
};


struct statistics_t {
  long cycles;
  long insns;
  long segments;
  struct timeval start_timeval;
};


extern struct countSpace_t countSpace;
extern struct statistics_t stats;
extern long frame_header;

extern struct ibuf_t* ib;
extern struct cache_t dcache;
extern struct fifo_t* trace_buffer;
extern struct fifo_t* l2;
extern int hart;
extern uint64_t mem_queue[tr_memq_len];

extern int timing;
extern int quiet;

extern long branch_penalty;
extern long fetch_latency;
extern long lg_ib_line;


void countSpace_init(const char* shm_name, int reader);

#undef insn
#define insn(pc)   ( &countSpace.insn_array[(pc-countSpace.base)/2].i )
#define count(pc)  ( &countSpace.insn_array[(pc-countSpace.base)/2]   )


extern long report_frequency;
void status_report(struct statistics_t* stats);

  
long dcache_writethru(long tr, const struct insn_t* p, long available);
long dcache_writeback(long tr, const struct insn_t* p, long available);

void fast_pipe(long pc, long read_latency, long next_report,
	       long (*model_dcache)(long tr, const struct insn_t* p, long available));
void slow_pipe(long pc, long read_latency, long next_report,
	       long (*model_dcache)(long tr, const struct insn_t* p, long available));
void count_pipe(long pc, long read_latency, long next_report,
		long (*model_dcache)(long tr, const struct insn_t* p, long available));

