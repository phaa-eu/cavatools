/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"


#define DEFAULT_REPORT_INTERVAL  1000000000


const char* tracing = 0;
const char* listing = 0;
struct core_t* cpu;
struct fifo_t verify;


int main(int argc, const char* argv[], const char* envp[])
{
  static const char* func = 0;
  static const char* after = 0;
  static const char* report = 0;
  static int quiet = 0;
  static struct options_t flags[] =
    {  { "--func=",	.v = &func		},
       { "--trace=",	.v = &tracing		},
       { "--verify=",	.v = &listing		},
       { "--after=",	.v = &after		},
       { "--report=",	.v = &report		},
       { "--quiet",	.f = &quiet		},
       { 0					}
    };
  int numopts = parse_options(flags, argv+1);
  
  Addr_t entry_pc = load_elf_binary(argv[1+numopts], 1);
  Addr_t stack_top = initialize_stack(argc-1-numopts, argv+1+numopts, envp);
  cpu = malloc(sizeof(struct core_t));
  dieif(cpu==0, "unable to malloc cpu");
  init_core(cpu);
  cpu->pc = entry_pc;
  cpu->reg[SP].a = stack_top;
  if (tracing || listing) {
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
  cpu->params.report_interval = report ? atoi(report) : DEFAULT_REPORT_INTERVAL;
  cpu->params.quiet = quiet;
  if (tracing) {
    trace_init(&cpu->tb, tracing, 0);
    fifo_put(&cpu->tb, trP(tr_start, 0, entry_pc));
    if (listing) {
      fifo_init(&verify, listing, 0);
      cpu->params.verify = 1;
    }
  }
  int rc = run_program(cpu);
  if (tracing) {
    fifo_flush(&cpu->tb);
    if (listing) {
      fifo_put(&verify, cpu->holding_pc);
      fifo_fini(&verify);
    }
    trace_fini(&cpu->tb);
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

