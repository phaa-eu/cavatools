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


#define DEFAULT_REPORT_INTERVAL  1000


struct core_t core;
  
const char* tracing;
static long bufsize;
static const char* func;

const struct options_t opt[] =
  {
   { "--out=s",		.s=&tracing,		.ds=0,		.h="Create trace file/fifo =name" },
   { "--trace=s",	.s=&tracing,		.ds=0,		.h="synonym for --out" },
   { "--buffer=i",	.i=&bufsize,		.di=12,		.h="Shared memory buffer size is 2^ =n bytes" },
   { "--func=s",	.s=&func,		.ds="_start",	.h="Trace function =name" },       
   { "--withregs",	.b=&core.params.flags,	.bv=tr_has_reg,	.h="Include register values in trace" },
   { "--after=i",	.i=&core.params.after,	.di=1,		.h="Start tracing function after =number calls" },
   { "--every=i",	.i=&core.params.every,	.di=1,		.h="Trace only every =number times function is called" },
   { "--skip=i",	.i=&core.params.skip,	.di=1,		.h="Trace function once every =number times called" },
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
  if (tracing) {
    if (!func)
      func = "_start";
    if (! find_symbol(func, &core.params.breakpoint, 0)) {
      fprintf(stderr, "function %s cannot be found in symbol table\n", func);
      exit(1);
    }
    fprintf(stderr, "Tracing %s at 0x%lx\n", func, core.params.breakpoint);
    core.params.flags |= tr_has_pc | tr_has_mem;
    core.tb = fifo_create(tracing, bufsize);
  }
  else
    core.params.breakpoint = 0;
  int rc = run_program(&core);
  if (tracing) {
    fifo_put(core.tb, trM(tr_eof, 0));
    fifo_finish(core.tb);
  }
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
  return outer_loop(&core);
}

