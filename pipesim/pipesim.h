/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#define COUNT  1

#define IBsize2		10	/* log-2 instruction buffer size (bytes) */
#define IBsize		(1<<IBsize2)

#define IBlinesz2	8	/* log-2 line size (bytes) */
#define IBnumlines	(1<<IBlinesz2)
#define IBlinemask	((IBsize/IBnumlines)-1)

#define IBblksz2	4	/* log-2 transfer block size (bytes) */
#define IBnumblks	(1<<(IBlinesz2-IBblksz2))
#define IBblkmask	(IBnumblks-1)

extern long fu_latency[];
extern long branch_delay;

extern struct cache_t sc;	/* shared cache */

extern struct fifo_t* in;	/* input fifo */
extern struct fifo_t* out;	/* output fifo (optional) */

extern int hart;
extern uint64_t mem_queue[tr_memq_len];

extern long quiet, report;


void perfCounters_init(const char* shm_name, int reader);
void status_report(long now, long icount, long ibmisses);
void simulate(long next_report);
