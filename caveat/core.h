/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
//#define DEBUG

#define PCTRACEBUFSZ	(1<<5)
#define MAXCALLDEPTH	(1<<6)

#define CLONESTACKSZ	(1<<16)


struct pctrace_t {
  Addr_t pc;			/* instruction program counter */
  struct reg_t regval;		/* value of rd (or rs2 for stores) */
};

struct callstack_t {		/* debuging call stack */
  Addr_t tgt;			/* address of called function */
  Addr_t ra;			/* return address */
};

/*
  HART definition
*/


typedef volatile struct {
  struct reg_t reg[64];		/* Register files, IR[0-31], FR[32-63] */
  Addr_t pc;			/* Next instruction to be executed */
    
  union {			/* floating-point control and status register */
    struct {
      unsigned flags : 5;	/* accrued exceptions NV,DZ,OF,UF,NX */
      unsigned rm    : 3;	/* rounding mode */
    } f;
    long l;
  } fcsr;

  int tid;			/* Linux thread id (not same as pthread) */
  int running;			/* main threads waits for zero */
  int exceptions;		/* bit mask of pending exceptions */
#define BREAKPOINT		0b00000001
#define EXIT_SYSCALL		0b00000010
#define STOP_SIMULATION		0b00000100
#define ILLEGAL_INSTRUCTION	0b00001000
#define ECALL_INSTRUCTION	0b00010000
  
  cache_t icache; /* instruction cache model */
  cache_t dcache; /* data cache model */
  long busy[256];		  /* time when register available */
  long cur_line;		  /* current insn cache line */
  
  struct {		     /* performance monitor structure */
    long cycle;		     /* machine cycle counter */
    long insn;		     /* machine instruction retired counter */
    long ecalls;	     /* machine environment call counter */
  } count;

#ifdef DEBUG
  struct {			/* debug structure */
    struct pctrace_t trace[PCTRACEBUFSZ];
    struct callstack_t stack[MAXCALLDEPTH];
    int tb;			/* pc trace circular buffer pointer */
    int cs;			/* call stack top */
  } debug;
#endif

} core_t;

/*
    The performance monitoring shared memory segment consists of:
	1.  Header struct (128B, below)
	2.  Array of struct core_t [cores]
	3.  Array of pre-decoded instructions
	4.  Arrays of CPI counters
	5.  Arrays of cache miss counters
    All arrays of dimension (bound-base)/2 parcels
*/
struct count_t {	      /* together for cache locality */
  long cycles;	      /* total including stalls */
  long count[3];     /* #times superscalar bundle n+1 insn */
};			      /* CPI = cycles/count */

struct perf_header_t {
  long size;		       /* of shared memory segment */
  long base, bound;	       /* text segment addresses  */
  int cores;		       /* maximum number of processor cores */
  int active;		       /* cores in use */
};

/*
    Pointers into shared memory segment.
*/
typedef volatile struct {
  struct perf_header_t* h;	/* shared memory header */
  struct insn_t* insn_array;	/* predecoded instructions */
  core_t* core;			/* core[0] is main thread */
  struct count_t** count;	/* count[i] = count array core i */
  long** icmiss;		/* icmiss[i][p] = core i address p */
  long** dcmiss;		/* icmiss[i][p] = core i address p */
} perf_t;


extern core_t* core;		/* main thread in core[0] */
extern char** clone_stack;	/* preallocated for cloning */

extern int active_cores; /* cores in use, maximum is conf.cores */
extern perf_t perf;    /* shared segment for performance monitoring */


void init_core(core_t* cpu);
void insert_breakpoint(Addr_t pc);
int interpreter(core_t* cpu);
int outer_loop(core_t* cpu);
void fast_sim(core_t*, long);
void slow_sim(core_t*, long);
void single_step(core_t*);
void proxy_ecall(core_t* cpu);
void proxy_csr(core_t* cpu, const struct insn_t* p, int which);
void status_report();
void final_status();
void parent_func(core_t* cpu);
int child_func(void* arg);
void perf_init(const char* shm_name, int reader);
void perf_close();
void print_pctrace(core_t* cpu);
void print_callstack(core_t* cpu);


#define  IR(rn)  cpu->reg[rn]	/* integer registers in 0..31 */
#define  FR(rn)  cpu->reg[rn]	/* floating point registers in 32..63 */
#define sex(rd)  IR(rd).l  = IR(rd).l  << 32 >> 32 /* sign extend integer register */
#define zex(rd)  IR(rd).ul = IR(rd).ul << 32 >> 32 /* zero extend integer register */
#define boxnan(rd)  cpu->reg[rd].ul |= 0xffffffff00000000UL

#ifdef SOFT_FP
#define  F32(rn)  cpu->reg[rn].f32
#define  F64(rn)  cpu->reg[rn].f64
static inline float32_t negateF32(float32_t x)  { x.v^=F32_SIGN; return x; }
static inline float64_t negateF64(float64_t x)  { x.v^=F64_SIGN; return x; }
#define  NF32(rn)  negateF32(cpu->reg[rn].f32)
#define  NF64(rn)  negateF64(cpu->reg[rn].f64)
#endif
