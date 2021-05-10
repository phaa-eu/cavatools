/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <stdint.h>
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
#include "cache.h"
#include "lru_fsm_1way.h"
#include "lru_fsm_2way.h"
#include "lru_fsm_3way.h"
#include "lru_fsm_4way.h"
#include "perfctr.h"

static long load_latency, fma_latency, branch_delay;

struct core_t core;
  
static const char *trace_path, *perf_path;

static long bufsize;
static const char* func;


const struct options_t opt[] =
  {
   { "--perf=s",	.s=&perf_path,		.ds=0,		.h="Performance counters in shared memory =name" },
   { "--trace=s",	.s=&trace_path,		.ds=0,		.h="Create cache miss trace file/fifo =name" },
   { "--buffer=i",	.i=&bufsize,		.di=12,		.h="Shared memory buffer size is 2^ =n bytes" },
   
   { "--func=s",	.s=&func,		.ds="_start",	.h="Trace function =name" },       
   { "--after=i",	.i=&core.params.after,	.di=1,		.h="Start tracing function after =number calls" },
   { "--every=i",	.i=&core.params.every,	.di=1,		.h="Trace only every =number times function is called" },
   { "--skip=i",	.i=&core.params.skip,	.di=1,		.h="Trace function once every =number times called" },
     
   { "--mhz=i",		.i=&core.params.mhz,	.di=1000,	.h="Pretend clock frequency =MHz" },
   { "--bdelay=i",	.i=&branch_delay,	.di=2,		.h="Taken branch delay is =number cycles" },
   { "--load=i",	.i=&load_latency,	.di=2,		.h="Load latency from cache" },
   { "--fma=i",		.i=&fma_latency,	.di=4,		.h="fused multiply add unit latency" },
     
   { "--imiss=i",	.i=&icache.penalty,	.di=5,		.h="L1 instruction cache miss latency is =number cycles" },
   { "--iline=i",	.i=&icache.lg_line,	.di=6,		.h="L1 instrucdtion cache line size is 2^ =n bytes" },
   { "--iways=i",	.i=&icache.ways,	.di=4,		.h="L1 instrucdtion cache is =n ways set associativity" },
   { "--isets=i",	.i=&icache.lg_rows,	.di=6,		.h="L1 instrucdtion cache has 2^ =n sets per way" },
     
   { "--dmiss=i",	.i=&dcache.penalty,	.di=4,		.h="L1 data cache miss latency is =number cycles" },
   { "--dline=i",	.i=&dcache.lg_line,	.di=6,		.h="L1 data cache line size is 2^ =n bytes" },
   { "--dways=i",	.i=&dcache.ways,	.di=4,		.h="L1 data cache is =w ways set associativity" },
   { "--dsets=i",	.i=&dcache.lg_rows,	.di=6,		.h="L1 data cache has 2^ =n sets per way" },
   
   { "--report=i",	.i=&core.params.report,	.di=1000,	.h="Progress report every =number million instructions" },
   { "--quiet",		.b=&core.params.quiet,	.bv=1,		.h="Don't report progress to stderr" },
   { "-q",		.b=&core.params.quiet,	.bv=1,		.h="Short for --quiet" },
   { "--sim",		.b=&core.params.simulate,.bv=1,		.h="Perform simulation" },
   { "--ecalls",	.b=&core.params.ecalls,	.bv=1,		.h="Log system calls" },
   { 0 }
  };
const char* usage = "caveat [caveat-options] target-program [target-options]";



#define mpy_cycles   8
#define div_cycles  32
#define fma_div_cycles (fma_latency*3)




int main(int argc, const char* argv[], const char* envp[])
{
  struct timeval start_timeval;
  gettimeofday(&start_timeval, 0);
  init_core(&core, clock(), &start_timeval);
  
  int numopts = parse_options(argv+1);
  if (argc == numopts+1)
    help_exit();
  core.params.report *= 1000000; /* unit is millions of instructions */

  //  printf("load_latency=%ld fma_latency=%ld\n", load_latency, fma_latency);
  for (int i=0; i<Number_of_opcodes; i++) {
    unsigned int a = insnAttr[i].flags;
    insnAttr[i].latency = (attr_l & a ? load_latency :
			   attr_M & a ? mpy_cycles :
			   attr_D & a ? div_cycles :
			   attr_E & a ? fma_div_cycles :
			   attr_f & a ? fma_latency : /* note after other FP flags */
			   1);
    //    if ((attr_l|attr_f) & a) printf("%s %d\n", insnAttr[i].name, insnAttr[i].latency);
  }
  insnAttr[Op_ecall   ].flags = ~0;
  insnAttr[Op_ebreak  ].flags = ~0;
  insnAttr[Op_c_ebreak].flags = ~0;

  Addr_t entry_pc = load_elf_binary(argv[1+numopts], 1);
  insnSpace_init();
  Addr_t stack_top = initialize_stack(argc-1-numopts, argv+1+numopts, envp);
  core.pc = entry_pc;
  core.reg[SP].a = stack_top;

  if (core.params.simulate) {
    if (!func)
      func = "_start";
    if (! find_symbol(func, &core.params.breakpoint, 0)) {
      fprintf(stderr, "function %s cannot be found in symbol table\n", func);
      exit(1);
    }

    /* initialize instruction cache */
    struct lru_fsm_t* fsm;
    switch (icache.ways) {
    case 1:  fsm = cache_fsm_1way;  break;
    case 2:  fsm = cache_fsm_2way;  break;
    case 3:  fsm = cache_fsm_3way;  break;
    case 4:  fsm = cache_fsm_4way;  break;
    default:  fprintf(stderr, "--iways=1..4 only\n");  exit(-1);
    }
    init_cache(&icache, "Instruction", fsm, 0);
  
    /* initialize data cache */
    switch (dcache.ways) {
    case 1:  fsm = cache_fsm_1way;  break;
    case 2:  fsm = cache_fsm_2way;  break;
    case 3:  fsm = cache_fsm_3way;  break;
    case 4:  fsm = cache_fsm_4way;  break;
    default:  fprintf(stderr, "--dways=1..4 only\n");  exit(-1);
    }
    init_cache(&dcache, "Data", fsm, 1);
  }

  core.params.sim_mode = sim_only;
  if (perf_path) {
    perf_create(perf_path);
    perf.start = start_timeval;
    core.params.sim_mode = sim_count;
  }
  if (trace_path) {
    trace = fifo_create(trace_path, bufsize);
    core.params.sim_mode = sim_trace;
  }
  if (perf_path && trace_path)
    core.params.sim_mode = sim_count_trace;
  int rc = run_program(&core);
  if (core.params.simulate) {
    print_cache(&icache, stdout);
    print_cache(&dcache, stdout);
  }
  if (perf_path)
    perf_close();
  if (trace_path) {
    fifo_put(trace, tr_eof);
    fifo_finish(trace);
  }
  return rc;
}



