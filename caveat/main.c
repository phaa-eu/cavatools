/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "cache.h"
#include "core.h"

struct conf_t conf;

const struct options_t opt[] =
  {
   { "--sim",		.b=&conf.simulate,	.bv=1,		.h="Perform simulation" },
   { "--func=s",	.s=&conf.func,		.ds="_start",	.h="Function =name to simulate" },
   { "--cores=i",	.i=&conf.cores,		.di=0,		.h="Number of simulated cores" },
   { "--report=i",	.i=&conf.report,	.di=100,	.h="Progress report every =number million cycles" },
   { "--perf=s",	.s=&conf.perf,		.ds="caveat",	.h="Performance counters in shared memory =name" },
   
   { "--ecalls",	.b=&conf.ecalls,	.bv=1,		.h="Log system calls" },
   { "--amo",		.b=&conf.amo,		.bv=1,		.h="Show AMO operations" },
   { "--visible",	.b=&conf.visible,	.bv=1,		.h="Print all instructions" },
   
   { "--after=i",	.i=&conf.after,		.di=1,		.h="Start tracing function after =number calls" },
   { "--every=i",	.i=&conf.every,		.di=1,		.h="Trace only every =number times function is called" },
     
   { "--mhz=i",		.i=&conf.mhz,		.di=1000,	.h="Pretend clock frequency =MHz" },
   { "--bdelay=i",	.i=&conf.branch,	.di=2,		.h="Taken branch delay is =number cycles" },
   { "--load=i",	.i=&conf.load,		.di=2,		.h="Load latency from cache" },
   { "--fma=i",		.i=&conf.fma,		.di=4,		.h="fused multiply add unit latency" },
     
   { "--imiss=i",	.i=&conf.ipenalty,	.di=5,		.h="L1 instruction cache miss latency is =number cycles" },
   { "--iline=i",	.i=&conf.iline,		.di=6,		.h="L1 instrucdtion cache line size is 2^ =n bytes" },
   { "--iways=i",	.i=&conf.iways,		.di=4,		.h="L1 instrucdtion cache is =n ways set associativity" },
   { "--isets=i",	.i=&conf.irows,		.di=6,		.h="L1 instrucdtion cache has 2^ =n sets per way" },
     
   { "--dmiss=i",	.i=&conf.dpenalty,	.di=4,		.h="L1 data cache miss latency is =number cycles" },
   { "--dline=i",	.i=&conf.dline,		.di=6,		.h="L1 data cache line size is 2^ =n bytes" },
   { "--dways=i",	.i=&conf.dways,		.di=4,		.h="L1 data cache is =w ways set associativity" },
   { "--dsets=i",	.i=&conf.drows,		.di=6,		.h="L1 data cache has 2^ =n sets per way" },
   
   { 0 }
  };
const char* usage = "caveat [caveat-options] target-program [target-options]";



#define mpy_cycles   8
#define div_cycles  32
#define fma_div_cycles (fma_latency*3)







#include <signal.h>
#include <setjmp.h>

jmp_buf return_to_top_level;

void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
  //  ucontext_t* context = (ucontext_t*)vcontext;
  //  context->uc_mcontext.gregs[]
  fprintf(stderr, "signal_handler(%d)\n", nSIGnum);
  longjmp(return_to_top_level, 1);
}


int main(int argc, const char* argv[], const char* envp[])
{
  int numopts = parse_options(argv+1);
  if (argc == numopts+1)
    help_exit();
  if (!conf.cores) {
    /* Environment OMP_NUM_THREADS sets default number of cores */
    const char* omp_num_threads = getenv("OMP_NUM_THREADS");
    fprintf(stderr, "OMP_NUM_THREADS = %s\n", omp_num_threads);
    if (omp_num_threads)
      conf.cores = atoi(omp_num_threads);
    else			/* is number of host cores */
      conf.cores = get_nprocs();
  }
  conf.report *= 1000000; /* unit is millions of instructions */
  for (int i=0; i<Number_of_opcodes; i++) {
    unsigned int a = insnAttr[i].flags;
    insnAttr[i].latency = (attr_l & a ? conf.load :
			   attr_M & a ? mpy_cycles :
			   attr_D & a ? div_cycles :

			   attr_f & a ? conf.fma : /* note after other FP flags */
			   1);
  }
  insnAttr[Op_ecall   ].flags = ~0;
  insnAttr[Op_ebreak  ].flags = ~0;
  insnAttr[Op_c_ebreak].flags = ~0;

  Addr_t entry_pc = load_elf_binary(argv[1+numopts], 1);
  Addr_t stack_top = initialize_stack(argc-1-numopts, argv+1+numopts, envp);
  if (conf.simulate) {
    if (!conf.func)
      conf.func = "_start";
    if (! find_symbol(conf.func, &conf.breakpoint, 0)) {
      fprintf(stderr, "function %s cannot be found in symbol table\n", conf.func);
      exit(1);
    }
    perf_init(conf.perf, conf.cores);
    insnSpace_init(perf.insn_array);
    core = perf.core;
    perf.h->active = 1;
  }
  else {
    insnSpace_init(0);
    core = (struct core_t*)malloc(conf.cores * sizeof(struct core_t));
    memset((void*)core, 0, conf.cores * sizeof(struct core_t));
  }

  clone_stack = malloc(conf.cores*sizeof(char*));
  for (int i=0; i<conf.cores; i++) {
    /* start with empty caches */
    init_cache(&core[i].icache, "Instruction", conf.ipenalty, conf.iways, conf.iline, conf.irows, 0);
    init_cache(&core[i].dcache, "Data",        conf.dpenalty, conf.dways, conf.dline, conf.drows, 1);
    /* pre-allocate stacks for cloning */
    clone_stack[i] = i ? malloc(CLONESTACKSZ) : 0;
  }
  core->tid = syscall(SYS_gettid);
  gettimeofday(&conf.start_tv, 0);
  for (int i=32; i<64; i++)	/* initialize FP registers to boxed float 0 */
    core->reg[i].ul = 0xffffffff00000000UL;
  core->pc = entry_pc;
  core->reg[SP].l = stack_top;

  static struct sigaction action;
  memset(&action, 0, sizeof(struct sigaction));
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  if (0) {
    action.sa_sigaction = signal_handler;
    sigaction(SIGSEGV, &action, NULL);
    if (setjmp(return_to_top_level) != 0) {
      /* which core was running when signal received? */
      pid_t tid = syscall(SYS_gettid);
      //      fprintf(stderr, "\n\nSegV %p tid=%d\n", si->si_addr, tid);
      struct core_t* cpu = 0;
      for (int i=0; i<conf.cores; i++) {
	// fprintf(stderr, "%score[%d] tid[%d] pc=%lx\n", color(core[i].tid), i, core[i].tid, core[i].pc);
	if (core[i].tid == tid)
	  cpu = &core[i];
      }
      if (!cpu) {
	fprintf(stderr, "Cannot find TID in cores\n");
	syscall(SYS_exit_group, -1);
      }
      fprintf(stderr, "%sSegV core[%ld] tid[%d]\n", color(cpu->tid), cpu-core, cpu->tid);
#ifdef DEBUG
      print_callstack(cpu);
      print_pctrace(cpu);
      print_registers(cpu->reg, stderr);
      syscall(SYS_exit_group, -1);
#endif
    }
  }
  
  if (conf.simulate)
    insert_breakpoint(conf.breakpoint);
  conf.fast_mode = 1;
  int rc = interpreter(&core[0]);
  final_status();
  if (conf.simulate)
    perf_close();
  return rc;
}



