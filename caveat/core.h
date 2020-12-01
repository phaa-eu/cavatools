/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#define sex(rd)  IR(rd).l  = IR(rd).l  << 32 >> 32
#define zex(rd)  IR(rd).ul = IR(rd).ul << 32 >> 32
#define box(rd)

extern unsigned long lrsc_set;  // globally shared location for atomic lock
extern long regval[];

struct core_t {
  struct fifo_t tb;
  struct reg_t reg[64];		// Register files, IR[0-31], FR[32-63]
#define IR(rn)  cpu->reg[rn]
#define FR(rn)  cpu->reg[rn]
  Addr_t pc;			// Next instruction to be executed
  Addr_t holding_pc;		// For verification tracing

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
    long start_tick;
    struct timeval start_timeval;
  } counter;

  struct {
    Addr_t breakpoint;		/* entrypoint of traced function */
    long after;			/* countdown, negative=start tracing */
    long every;			/* but only trace once per n-1 calls */
    long skip;			/* skip until negative, reset to every */
    long report_interval;
    long has_flags;
    int quiet;
  } params;
};


extern struct fifo_t verify;

void init_core(struct core_t* cpu);
int run_program(struct core_t* cpu);
int outer_loop(struct core_t* cpu);
void fast_sim(struct core_t*, long max_count);
void slow_sim(struct core_t*, long max_count);
int proxy_ecall( struct core_t* cpu );
void proxy_csr( struct core_t* cpu, const struct insn_t* p, int which );
void status_report(struct core_t* cpu, FILE*);

