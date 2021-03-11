/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/



struct count_t {	      /* together for cache locality */
  long cycles;		      /* total including stalls */
  long count[3];	      /* #times superscalar bundle n+1 insn */
};			      /* CPI = cycles/count */

/*
    The performance monitoring shared memory segment consists of:
	1.  Header struct (128B, below)
	2.  Array of pre-decoded instructions and counts (above)
	3.  Array of per-instruction ib_miss
	4.  Array of per-instruction ic_miss
	5.  Array of per-instruction dc_miss
	6.  Copy of text segment (size bound-base bytes)
    All arrays of dimension (bound-base)/2
*/
struct perf_header_t {
  long base, bound;		/* text segment addresses  */
  long size;			/* of shared memory segment */
  long pad1[8-3];		/* read-only stuff in own 64B cache line */
  long ib_misses;		/* number of instruction buffer misses */
  long ic_misses;		/* number of instruction cache misses */
  long dc_misses;		/* number of data cache misses */
  long insns;			/* number of instructions executed */
  long cycles;			/* number of cycles simulated */
  long segments;		/* number of disjoint trace segments */
  long pad2[8-6];		/* rapidly updated stuff in own cache line */
};


/*
    Base pointers into shared memory segment.
*/
struct perfCounters_t {
  struct perf_header_t* h;	/* shared memory header */
  struct count_t* count_array;	/* predecoded instruction & counts */
  long* ib_miss;		/* counts instruction buffer miss */
  long* ic_miss;		/* counts instruction cache miss */
  long* dc_miss;		/* counts data cache miss */
  struct timeval start;		/* time of day when program started */
};

extern struct perfCounters_t perf;

static inline struct count_t* count(long pc)  { return &perf.count_array[(pc-perf.h->base)/2]; }
static inline long* ibmiss(long pc)  { return &perf.ib_miss[(pc-perf.h->base)/2]; }
static inline long* icmiss(long pc)  { return &perf.ic_miss[(pc-perf.h->base)/2]; }
static inline long* dcmiss(long pc)  { return &perf.dc_miss[(pc-perf.h->base)/2]; }

void perf_create(const char* shm_name);
void perf_open(const char* shm_name);
void perf_close();

