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


const char* tracing = 0;
struct core_t* cpu;


int main(int argc, const char* argv[], const char* envp[])
{
  struct timeval start_timeval;
  gettimeofday(&start_timeval, 0);
  long start_tick = clock();
  
  static const char* func = 0;
  static const char* after = 0;
  static const char* every = 0;
  static const char* report = 0;
  static int withregs = 0;
  static int quiet = 0;
  static struct options_t opt[] =
    {
     { "--out=",	.v=&tracing,	.h="Create trace file/fifo =name [no trace]" },
     { "--trace=",	.v=&tracing,	.h="synonym for --out" },
     { "--func=",	.v=&func,	.h="Trace function =name [_start]" },       
     { "--withregs",	.f=&withregs,	.h="Include register values in trace" },
     { "--after=",	.v=&after,	.h="Start tracing function after =number calls [1]" },
     { "--every=",	.v=&every,	.h="Trace only every =number times function is called [1]" },
     { "--report=",	.v=&report,	.h="Progress report every =number million instructions [1000]" },
     { "--quiet",	.f=&quiet,	.h="Don't report progress to stderr" },
     { "-q",		.f=&quiet,	.h="short for --quiet" },
     { 0 }
    };
  int numopts = parse_options(opt, argv+1,
			      "caveat [caveat-options] target-program [target-options]");
  if (argc == numopts+1)
    help_exit();
  Addr_t entry_pc = load_elf_binary(argv[1+numopts], 1);
  Addr_t stack_top = initialize_stack(argc-1-numopts, argv+1+numopts, envp);
  cpu = malloc(sizeof(struct core_t));
  dieif(cpu==0, "unable to malloc cpu");
  init_core(cpu, start_tick, &start_timeval);
  cpu->pc = entry_pc;
  cpu->reg[SP].a = stack_top;
  if (tracing) {
    if (!func)
      func = "_start";
    if (! find_symbol(func, &cpu->params.breakpoint, 0)) {
      fprintf(stderr, "function %s cannot be found in symbol table\n", func);
      exit(1);
    }
    fprintf(stderr, "Tracing %s at 0x%lx\n", func, cpu->params.breakpoint);
  }
  else
    cpu->params.breakpoint = 0;
  cpu->params.after = after ? atoi(after) : 0;
  cpu->params.every = every ? atoi(after) : 1;
  cpu->params.skip = cpu->params.every-1;
  cpu->params.report_interval = (report ? atoi(report) : DEFAULT_REPORT_INTERVAL) * 1000000;
  cpu->params.quiet = quiet;
  if (tracing) {
    cpu->params.has_flags = tr_has_pc | tr_has_mem;
    if (withregs)
      cpu->params.has_flags |= tr_has_reg;
    cpu->tb = fifo_create(tracing, 0);
    //cpu->tb = fifo_create(tracing, 20);
  }
  int rc = run_program(cpu);
  if (tracing) {
    fifo_put(cpu->tb, trM(tr_eof, 0));
    fifo_finish(cpu->tb);
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
  return outer_loop(cpu);
}

