/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


extern unsigned long lrsc_set;  // globally shared location for atomic lock

struct core_t {
  struct reg_t reg[64];		// Register files, IR[0-31], FR[32-63]
  Addr_t pc;			// Next instruction to be executed

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
  
  struct {
    long insn_executed;
    long cycles_simulated;
    long ecalls;
    long start_tick;
    struct timeval start_timeval;
  } counter;

  struct {
    Addr_t breakpoint;		/* entrypoint of traced function */
    long after;			/* countdown, negative=start pipeline simulation */
    long every;			/* but only trace once per n-1 calls */
    long skip;			/* skip until negative, reset to every */
    long report;
    //    long flags;
    long quiet;
    long mhz;			/* pretend clock MHz */
    long simulate;		/* do performance counting */
    long ecalls;		/* log system calls */
    long visible;		/* show every instruction */
  } params;
};


extern struct cache_t icache;	/* instruction cache model */
extern struct cache_t dcache;	/* data cache model */
extern struct fifo_t* trace;	/* of L2 references */


void init_core(struct core_t* cpu, long start_tick, const struct timeval* start_timeval);
int run_program(struct core_t* cpu);
int outer_loop(struct core_t* cpu);
void fast_sim(struct core_t*);
void only_sim(struct core_t*);
void count_sim(struct core_t*);
void trace_sim(struct core_t*);
void count_trace_sim(struct core_t*);
void single_step();
int proxy_ecall( struct core_t* cpu );
void proxy_csr( struct core_t* cpu, const struct insn_t* p, int which );
void status_report(struct core_t* cpu, FILE*);



#define  IR(rn)  cpu->reg[rn]
#define  FR(rn)  cpu->reg[rn]
#define sex(rd)  IR(rd).l  = IR(rd).l  << 32 >> 32
#define zex(rd)  IR(rd).ul = IR(rd).ul << 32 >> 32
#define box(rd)

#ifdef SOFT_FP
#define  F32(rn)  cpu->reg[rn].f32
#define  F64(rn)  cpu->reg[rn].f64
#define  NF32(rn)  negateF32(cpu->reg[rn].f32)
#define  NF64(rn)  negateF64(cpu->reg[rn].f64)
static inline float32_t negateF32(float32_t x)  { x.v^=F32_SIGN; return x; }
static inline float64_t negateF64(float64_t x)  { x.v^=F64_SIGN; return x; }
#endif
