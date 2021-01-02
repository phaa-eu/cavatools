/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/



/*
  Instruction buffer is two-line cache with subblocking.
*/
struct ibuf_t {
  long tag[2];
  long* ready[2];		/* ready[2][numblks] */
  long tag_mask;		/* pc mask = (1 << lg_line) - 1 */
  long blk_mask;		/* block index mask = numblks - 1 */
  long subblockmask;		/* = ~0UL << ib->lg_blksize */
  long curblk;			/* pc of current ibuffer subblock */
  long misses;			/* number of misses */
  long bufsz;			/* log-base-2 of capacity (bytes) */
  long blksize;			/* log-base-2 of block size (bytes) */
  long numblks;			/* = (1<<lg_line)/(1<<lg_blksize) */
  long penalty;			/* cycles to refill critical block */
  long delay;			/* taken branch delay (pipeline flush) */
  int mru;			/* which tag is most recently used */
};

extern struct ibuf_t  ib;	/* instruction buffer model */
extern struct cache_t ic;	/* instruction cache model */
extern struct cache_t dc;	/* data cache model */

extern struct fifo_t* in;	/* input fifo */
extern struct fifo_t* out;	/* output fifo (optional) */

extern int hart;
extern uint64_t mem_queue[tr_memq_len];

extern long quiet, report;


void perfCounters_init(const char* shm_name, int reader);
void status_report(long now, long icount);

void fast_pipe(long next_report, long (*model_dcache)(long tr, const struct insn_t* p, long available));
void trace_pipe(long next_report, long (*model_dcache)(long tr, const struct insn_t* p, long available));
void count_pipe(long next_report, long (*model_dcache)(long tr, const struct insn_t* p, long available));
void trace_count_pipe(long next_report, long (*model_dcache)(long tr, const struct insn_t* p, long available));

long dcache_writethru(long tr, const struct insn_t* p, long available);
long dcache_writeback(long tr, const struct insn_t* p, long available);
