/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern unsigned long lrsc_set;  // globally shared location for atomic lock

/*
  HART definition
*/
struct core_t {
  struct reg_t reg[64];		/* Register files, IR[0-31], FR[32-63] */
  Addr_t pc;			/* Next instruction to be executed */
  int tid;			/* Linux thread id (not same as pthread) */

  struct {
    long coreid;
    long ustatus;
    long mcause;
    Addr_t mepc;
    long mtval;
    union {
      struct {
	unsigned flags	:  5;
	unsigned rmode	:  3;
      } fcsr;
      unsigned long fcsr_v;
    };
  } state;
  
  struct cache_t icache;	/* instruction cache model */
  struct cache_t dcache;	/* data cache model */
  
  struct {
    long insn_executed;
    long cycles_simulated;
    long ecalls;
    long start_tick;
    struct timeval start_timeval;
    long after;			/* countdown, negative=start pipeline simulation */
    long skip;			/* skip until negative, reset to every */
    long visible;
  } perf;
};

/*
    The performance monitoring shared memory segment consists of:
	1.  Header struct (128B, below)
	2.  Array of pre-decoded instructions
	3.  Array of struct core_t [cores]
	4.  Arrays of CPI counters
	4.  Arrays of cache miss counters
    All arrays of dimension (bound-base)/2 parcels
*/
struct count_t {	      /* together for cache locality */
  long cycles;		      /* total including stalls */
  long count[3];	      /* #times superscalar bundle n+1 insn */
};			      /* CPI = cycles/count */

struct perf_header_t {
  long size;			/* of shared memory segment */
  long cores;			/* maximum number of processor cores */
  long active;			/* cores in use */
  long base, bound;		/* text segment addresses  */
};

/*
    Pointers into shared memory segment.
*/
struct perf_t {
  struct perf_header_t* h;	/* shared memory header */
  struct insn_t* insn_array;	/* predecoded instructions */
  struct core_t* core;		/* core[0] is main thread */
  struct count_t** count;	/* count[i] = count array core i */
  long** icmiss;		/* icmiss[i][p] = core i address p */
  long** dcmiss;		/* icmiss[i][p] = core i address p */
};


extern struct core_t* core;	/* main thread in core[0] */
extern int active_cores;	/* cores in use, maximum is conf.cores */
extern struct perf_t perf;	/* shared segment for performance monitoring */


void init_core(struct core_t* cpu, struct core_t* parent, Addr_t entry_pc, Addr_t stack_top, Addr_t thread_ptr);
int run_program(struct core_t* cpu);
int outer_loop(struct core_t* cpu);
void fast_sim(struct core_t*);
void slow_sim(struct core_t*);
void single_step(struct core_t*);
int proxy_ecall( struct core_t* cpu );
void proxy_csr( struct core_t* cpu, const struct insn_t* p, int which );
void status_report(struct core_t* cpu, FILE*);
void final_status();
void parent_func(struct core_t* cpu);
int child_func(void* arg);
void perf_init(const char* shm_name, int reader);
void perf_close();


#define  IR(rn)  cpu->reg[rn]	/* integer registers in 0..31 */
#define  FR(rn)  cpu->reg[rn]	/* floating point registers in 32..63 */
#define sex(rd)  IR(rd).l  = IR(rd).l  << 32 >> 32 /* sign extend integer register */
#define zex(rd)  IR(rd).ul = IR(rd).ul << 32 >> 32 /* zero extend integer register */
#define box(rd)					   /* extend single precision float to 64 bits */

#ifdef SOFT_FP
#define  F32(rn)  cpu->reg[rn].f32
#define  F64(rn)  cpu->reg[rn].f64
static inline float32_t negateF32(float32_t x)  { x.v^=F32_SIGN; return x; }
static inline float64_t negateF64(float64_t x)  { x.v^=F64_SIGN; return x; }
#define  NF32(rn)  negateF32(cpu->reg[rn].f32)
#define  NF64(rn)  negateF64(cpu->reg[rn].f64)
#endif
