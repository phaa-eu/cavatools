/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define COUNT  1

extern long branch_delay;

extern struct cache_t ic, dc;	/*  I and D caches */

extern struct fifo_t* in;	/* input fifo */
extern struct fifo_t* out;	/* output fifo (optional) */

extern int hart;
extern uint64_t mem_queue[tr_memq_len];

extern long quiet, report;


void perfCounters_init(const char* shm_name, int reader);
void status_report(long now, long icount, long ibmisses);
void simulate(long next_report);
