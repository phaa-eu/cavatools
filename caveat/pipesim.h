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
extern struct cache_t icache;	/* instruction cache model */
extern struct cache_t dcache;	/* data cache model */

void perfCounters_init(const char* shm_name, int reader);
