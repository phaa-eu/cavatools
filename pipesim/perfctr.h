/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/



struct count_t {		/* together for cache locality */
  struct insn_t i;		/* decoded instruction */
  long count;			/* how many times executed */
  long cycles;			/* total including stalls */
};				/* CPI = cycles/count */

struct perf_params_t {
  Addr_t base, bound;		/* text segment addresses  */
  size_t size;			/* of shared memory segment */
};

/*
    The performance monitoring shared memory segment consists of:
	1.  Header struct (below)
	2.  Array of pre-decoded instructions and counts (above)
	3.  Array of per-instruction ib_miss
	4.  Array of per-instruction ic_miss
	5.  Array of per-instruction dc_miss
    All arrays of dimension (bound-base)/2
*/
struct perf_header_t {
  struct perf_params_t p;
  long ib_misses;		/* number of instruction buffer misses */
  long ic_misses;		/* number of instruction cache misses */
  long dc_misses;		/* number of data cache misses */
  long insns;			/* number of instructions executed */
  long cycles;			/* number of cycles simulated */
  long segments;		/* number of disjoint trace segments */
};


/*
    Base pointers into shared memory segment.
*/
struct perfCounters_t {
  struct perf_params_t p;
  struct perf_header_t* h;	/* shared memory header */
  struct count_t* count_array;	/* predecoded instruction & counts */
  long* ib_miss;		/* counts instruction buffer miss */
  long* ic_miss;		/* counts instruction cache miss */
  long* dc_miss;		/* counts data cache miss */
  struct timeval start;		/* time of day when program started */
};

extern struct perfCounters_t perf;

#undef insn
#define insn(pc)   ( &perf.count_array[(pc-perf.p.base)/2].i )

static inline const struct count_t* count(long pc)  { return &perf.count_array[(pc-perf.p.base)/2]; }
static inline const long* ibmiss(long pc)  { return &perf.ib_miss[(pc-perf.p.base)/2]; }
static inline const long* icmiss(long pc)  { return &perf.ic_miss[(pc-perf.p.base)/2]; }
static inline const long* dcmiss(long pc)  { return &perf.dc_miss[(pc-perf.p.base)/2]; }

void perf_create(const char* shm_name);
void perf_open(const char* shm_name);
void perf_close();

