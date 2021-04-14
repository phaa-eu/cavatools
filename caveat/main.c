/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"
#include "pipesim.h"
#include "cache.h"
#include "lru_fsm_1way.h"
#include "lru_fsm_2way.h"
#include "lru_fsm_3way.h"
#include "lru_fsm_4way.h"
#include "perfctr.h"

unsigned char fu_latency[Number_of_units] =
  { [Unit_a] = 4,	/* FP Adder */
    [Unit_b] = 1,	/* Branch unit */
    [Unit_f] = 4,	/* FP fused Multiply-Add */
    [Unit_i] = 1,	/* Scalar Integer ALU */
    [Unit_j] = 1,	/* Media Integer ALU */
    [Unit_m] = 4,	/* FP Multipler*/
    [Unit_n] = 8,	/* Scalar Integer Multipler */
    [Unit_r] = 2,	/* Load unit */
    [Unit_s] = 1,	/* Scalar Shift unit */
    [Unit_t] = 1,	/* Media Shift unit */
    [Unit_w] = 1,	/* Store unit */
    [Unit_x] = 5,	/* Special unit */
  };


struct core_t core;
  
static const char* tracing;
static const char *perf_path;

static long bufsize;
static const char* func;


const struct options_t opt[] =
  {
   { "--out=s",		.s=&tracing,		.ds=0,		.h="Create trace file/fifo =name" },
   { "--buffer=i",	.i=&bufsize,		.di=12,		.h="Shared memory buffer size is 2^ =n bytes" },
   { "--perf=s",	.s=&perf_path,		.ds=0,		.h="Performance counters in shared memory =name" },
   
   { "--func=s",	.s=&func,		.ds="_start",	.h="Trace function =name" },       
   { "--after=i",	.i=&core.params.after,	.di=1,		.h="Start tracing function after =number calls" },
   { "--every=i",	.i=&core.params.every,	.di=1,		.h="Trace only every =number times function is called" },
   { "--skip=i",	.i=&core.params.skip,	.di=1,		.h="Trace function once every =number times called" },

   { "--bdelay=i",	.i=&ib.delay,		.di=2,		.h="Taken branch delay is =number cycles" },
   { "--bmiss=i",	.i=&ib.penalty,		.di=5,		.h="L0 instruction buffer refill latency is =number cycles" },
   { "--bufsz=i",	.i=&ib.bufsz,		.di=7,		.h="L0 instruction buffer capacity is 2*2^ =n bytes" },
   { "--blocksz=i",	.i=&ib.blksize,		.di=4,		.h="L0 instruction buffer block size is 2^ =n bytes" },
     
   { "--imiss=i",	.i=&ic.penalty,		.di=25,		.h="L1 instruction cache miss latency is =number cycles" },
   { "--iline=i",	.i=&ic.lg_line,		.di=6,		.h="L1 instrucdtion cache line size is 2^ =n bytes" },
   { "--iways=i",	.i=&ic.ways,		.di=4,		.h="L1 instrucdtion cache is =n ways set associativity" },
   { "--isets=i",	.i=&ic.lg_rows,		.di=6,		.h="L1 instrucdtion cache has 2^ =n sets per way" },
     
   { "--dmiss=i",	.i=&dc.penalty,		.di=25,		.h="L1 data cache miss latency is =number cycles" },
   { "--dline=i",	.i=&dc.lg_line,		.di=6,		.h="L1 data cache line size is 2^ =n bytes" },
   { "--dways=i",	.i=&dc.ways,		.di=4,		.h="L1 data cache is =w ways set associativity" },
   { "--dsets=i",	.i=&dc.lg_rows,		.di=6,		.h="L1 data cache has 2^ =n sets per way" },
   
   { "--report=i",	.i=&core.params.report,	.di=1000,	.h="Progress report every =number million instructions" },
   { "--quiet",		.b=&core.params.quiet,	.bv=1,		.h="Don't report progress to stderr" },
   { "-q",		.b=&core.params.quiet,	.bv=1,		.h="short for --quiet" },
   { 0 }
  };
const char* usage = "caveat [caveat-options] target-program [target-options]";

int main(int argc, const char* argv[], const char* envp[])
{
  struct timeval start_timeval;
  gettimeofday(&start_timeval, 0);
  for (int i=0; i<Number_of_opcodes; i++)
    insnAttr[i].latency = fu_latency[insnAttr[i].unit];
  init_core(&core, clock(), &start_timeval);

  int numopts = parse_options(argv+1);
  if (argc == numopts+1)
    help_exit();
  core.params.report *= 1000000; /* unit is millions of instructions */
  Addr_t entry_pc = load_elf_binary(argv[1+numopts], 1);
  insnSpace_init();
  Addr_t stack_top = initialize_stack(argc-1-numopts, argv+1+numopts, envp);
  core.pc = entry_pc;
  core.reg[SP].a = stack_top;
  
  if (!func)
    func = "_start";
  if (! find_symbol(func, &core.params.breakpoint, 0)) {
    fprintf(stderr, "function %s cannot be found in symbol table\n", func);
    exit(1);
  }
  fprintf(stderr, "Tracing %s at 0x%lx\n", func, core.params.breakpoint);
  core.params.flags |= tr_has_pc | tr_has_mem;

  perf_create(perf_path);
  perf.start = start_timeval;
  
  /* initialize instruction buffer */
  ib.tag_mask = ~( (1L << (ib.bufsz-1)) - 1 );
  ib.numblks = (1<<ib.bufsz)/(1<<ib.blksize) - 1;
  ib.blk_mask = ib.numblks - 1;
  for (int i=0; i<2; i++) {
    ib.ready[i] = (long*)malloc(ib.numblks*sizeof(long));
    memset((char*)ib.ready[i], 0, ib.numblks*sizeof(long));
  }

  /* initialize instruction cache */
  struct lru_fsm_t* fsm;
  switch (ic.ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--iways=1..4 only\n");  exit(-1);
  }
  init_cache(&ic, fsm, 0);
  
  /* initialize data cache */
  switch (dc.ways) {
  case 1:  fsm = cache_fsm_1way;  break;
  case 2:  fsm = cache_fsm_2way;  break;
  case 3:  fsm = cache_fsm_3way;  break;
  case 4:  fsm = cache_fsm_4way;  break;
  default:  fprintf(stderr, "--dways=1..4 only\n");  exit(-1);
  }
  init_cache(&dc,fsm, 1);
  
  int rc = run_program(&core);
  return rc;
}



#include <signal.h>
#include <setjmp.h>

jmp_buf return_to_top_level;

void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
//  ucontext_t* context = (ucontext_t*)vcontext;
//  context->uc_mcontext.gregs[]
  fprintf(stderr, "\n\nSegV %p\n", si->si_addr);
  longjmp(return_to_top_level, 1);
}

int run_program(struct core_t* cpu)
{
  struct sigaction action;
  memset(&action, 0, sizeof(struct sigaction));
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
//  action.sa_flags = SA_SIGINFO;
  action.sa_sigaction = signal_handler;
  action.sa_flags = 0;

  sigaction(SIGSEGV, &action, NULL);
  if (setjmp(return_to_top_level) != 0) {
    //fprintf(stderr, "Back to main\n");
    print_insn(cpu->pc, stderr);
    print_registers(cpu->reg, stderr);
    return -1;
  }

  if (cpu->params.breakpoint)
    insert_breakpoint(cpu->params.breakpoint);
  int fast_mode = 1;
  cpu->holding_pc = 0;
  while (1) {	       /* terminated by program making exit() ecall */
    long next_report = (cpu->counter.insn_executed+cpu->params.report) / cpu->params.report;
    next_report = next_report*cpu->params.report - cpu->counter.insn_executed;
    if (fast_mode)
      fast_sim(cpu, next_report);
    else
      slow_sim(cpu, next_report);
    if (cpu->state.mcause != 3) /* Not breakpoint */
      break;
    if (fast_mode) {
      if (--cpu->params.after > 0 || /* not ready to trace yet */
	  --cpu->params.skip > 0) {  /* only trace every n call */
	cpu->holding_pc = 0L;	/* do not include current pc */
	cpu->params.skip = cpu->params.every;
	/* put instruction back */
	decode_instruction(insn(cpu->pc), cpu->pc);
	cpu->state.mcause = 0;
	fast_sim(cpu, 1);	/* single step */
	/* reinserting breakpoint at subroutine entry */	  
	insert_breakpoint(cpu->params.breakpoint);
      }
      else { /* insert breakpoint at subroutine return */
	if (cpu->reg[RA].a)	/* _start called with RA==0 */
	  insert_breakpoint(cpu->reg[RA].a);
	fast_mode = 0;		/* start tracing */
      }
    }
    else {  /* reinserting breakpoint at subroutine entry */
      insert_breakpoint(cpu->params.breakpoint);
      fast_mode = 1;		/* stop tracing */
      cpu->holding_pc = 0L;	/* do not include current pc */
    }
    cpu->state.mcause = 0;
    decode_instruction(insn(cpu->pc), cpu->pc);
  }
  if (cpu->state.mcause == 8) { /* only exit() ecall not handled */
    cpu->counter.insn_executed++;	/* don't forget to count last ecall */
    if (!cpu->params.quiet) {
      clock_t end_tick = clock();
      double elapse_time = (end_tick - cpu->counter.start_tick)/CLOCKS_PER_SEC;
      double mips = cpu->counter.insn_executed / (1e6*elapse_time);
      fprintf(stderr, "\n\nExecuted %ld instructions in %3.1f seconds for %3.1f MIPS\n",
	      cpu->counter.insn_executed, elapse_time, mips);
    }
    return cpu->reg[10].i;
  }
  /* The following cases do not fall out */
  switch (cpu->state.mcause) {
  case 2:
    fprintf(stderr, "Illegal instruction at 0x%08lx\n", cpu->pc);
    GEN_SEGV;
  case 10:
    fprintf(stderr, "Unknown instruction at 0x%08lx\n", cpu->pc);
    GEN_SEGV;
  default:
    abort();
  }
}


