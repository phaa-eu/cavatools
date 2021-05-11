/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "core.h"
#include "cache.h"


/*
  int clone(int (*fn)(void *arg), void *child_stack, int flags, void *arg,
  void *parent_tidptr, void *tls, void *child_tidptr);

  RISC-V clone system call arguments:
  a0 = fn
  a1 = child_stack
  a2 = flags
  a3 = parent_tidptr
  a4 = tls
  a5 = child_tidptr
*/

static pthread_cond_t  clone_condv;
static pthread_mutex_t clone_mutex;
static struct core_t* childcpu;	/* protected by mutex */


void parent_func(struct core_t* parent)
{
  static pthread_t childthread;
  pthread_mutex_lock(&clone_mutex);
  dieif(pthread_cond_init(&clone_condv, NULL), "pthread_cond_init");
  dieif(pthread_create(&childthread, NULL, child_func, parent), "pthread_create");
  pthread_cond_wait(&clone_condv, &clone_mutex);
  parent->reg[10].l = childcpu->tid;
  pthread_mutex_unlock(&clone_mutex);
}

void* child_func(void* arg)
{
  struct core_t* parent = (struct core_t*)arg;
  struct core_t* cpu = malloc(sizeof(struct core_t));
  *cpu = *parent;		/* initialize child registers to same */
  cpu->next = maincpu.next;	/* add child to cpu list */
  maincpu.next = cpu;
  cpu->parent = parent;
  cpu->pc += 4;			/* skip over ecall instruction */
  cpu->reg[2] = cpu->reg[11];	/* child sp from a1 */
  cpu->reg[4] = cpu->reg[14];	/* child tp from a4 */
  cpu->reg[10].l = 0;		/* child a0=0 indicating I am child */
  pthread_mutex_lock(&clone_mutex);
  cpu->tid = syscall(SYS_gettid);
  childcpu = cpu;
  pthread_cond_signal(&clone_condv);
  pthread_mutex_unlock(&clone_mutex);
  int rc = run_program(cpu);
  fprintf(stderr, "child %d about to exit\n", cpu->tid);
  pthread_exit(NULL);
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

void final_stats()
{
  if (simparam.quiet)
    return;
  clock_t end_tick = clock();
  fprintf(stderr, "\n\n");
  for (struct core_t* cpu=&maincpu; cpu; cpu=cpu->next) {
    double elapse_time = (end_tick - cpu->counter.start_tick)/CLOCKS_PER_SEC;
    double mips = cpu->counter.insn_executed / (1e6*elapse_time);
    fprintf(stderr, "%sThread %d executed %ld instructions (%ld system calls) in %3.1f seconds for %3.1f MIPS\e[39m\n",
	    color[cpu->tid%8], cpu->tid, cpu->counter.insn_executed, cpu->counter.ecalls, elapse_time, mips);
  }
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
    print_insn(cpu->pc, stderr);
    print_registers(cpu->reg, stderr);
    return -1;
  }

  if (cpu->params.breakpoint)
    insert_breakpoint(cpu->params.breakpoint);
  int fast_mode = 1;
  while (1) {	       /* terminated by program making exit() ecall */
    if (fast_mode)
      fast_sim(cpu);
    else switch (simparam.sim_mode) {
      case sim_only:
	only_sim(cpu);
	break;
      case sim_count:
	count_sim(cpu);
	break;
      case sim_trace:
	trace_sim(cpu);
	break;
      case sim_count_trace:
	count_trace_sim(cpu);
	break;
      default:
	abort();
      }
    if (cpu->state.mcause != 3) /* Not breakpoint */
      break;
    if (fast_mode) {
      if (--cpu->params.after > 0 || /* not ready to trace yet */
	  --cpu->params.skip > 0) {  /* only trace every n call */
	cpu->params.skip = cpu->params.every;
	/* put instruction back */
	decode_instruction(insn(cpu->pc), cpu->pc);
	cpu->state.mcause = 0;
	single_step(cpu);
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
    }
    cpu->state.mcause = 0;
    decode_instruction(insn(cpu->pc), cpu->pc);
  }
  if (cpu->state.mcause == 8) { /* only exit() ecall not handled */
    //cpu->counter.insn_executed++;	/* don't forget to count last ecall */
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
