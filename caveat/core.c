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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <signal.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <sys/times.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "imacros.h"
#include "cache.h"
#include "core.h"

volatile int active_cores = 1;	/* main thread */
char** clone_stack;
volatile int amosemi[AMOHASH]; /* 0=unlock, 1=lock with no waiters, 2=lock with waiters */
volatile unsigned long lrscstate; /* current load reserve address */

int interpreter(struct core_t* cpu)
{
  while (1) {	       /* terminated by program making exit() ecall */
    if (conf.fast_mode)
      fast_sim(cpu, conf.report);
    else
      slow_sim(cpu, conf.report);
    if (!cpu->exceptions && !conf.quiet)
      status_report();
    /* process all pending exceptions */
    while (cpu->exceptions) {
      if (cpu->exceptions & ECALL_INSTRUCTION) {
	if (insn(cpu->pc)->op_code != Op_ecall) {
	  fprintf(stderr, "core[%ld] ecall not at ecall!\n", cpu-core);
	  print_pc(cpu->pc, stderr);
	  print_insn(cpu->pc, stderr);
	  exit(-2);
	}
	proxy_ecall(cpu);
	cpu->pc += 4;
	__sync_fetch_and_and(&cpu->exceptions, ~ECALL_INSTRUCTION);
      }
      else if (cpu->exceptions & BREAKPOINT) {
	if (conf.fast_mode) {
	  if (--conf.after > 0) { /* not ready to trace yet */
	    //fprintf(stderr, "interpreter breakpoint: not ready to trace\n");
	    /* put instruction back */
	    decode_instruction(insn(cpu->pc), cpu->pc);
	    fast_sim(cpu, 1);	/* single step */
	    /* reinserting breakpoint at subroutine entry */
	    insert_breakpoint(conf.breakpoint);
	    /* simulate every nth call */
	  }
	  else { /* insert breakpoint at subroutine return */
	    //fprintf(stderr, "interpreter breakpoint: inserting breakpoint at return\n");
	    if (cpu->reg[RA].a)	/* _start called with RA==0 */
	      insert_breakpoint(cpu->reg[RA].a);
	    conf.fast_mode = 0;		/* start simulation */
	  }
	}
	else {  /* reinserting breakpoint at subroutine entry */
	  //fprintf(stderr, "interpreter breakpoint: at return, re-inserting breakpoint\n");
	  insert_breakpoint(conf.breakpoint);
	  conf.fast_mode = 1;		/* stop tracing */
	  conf.after = conf.every;
	}
	decode_instruction(insn(cpu->pc), cpu->pc);
	__sync_fetch_and_and(&cpu->exceptions, ~BREAKPOINT);
      }
      else if (cpu->exceptions & (EXIT_SYSCALL|STOP_SIMULATION)) {
	cpu->running = 0;
	syscall(SYS_futex, &cpu->running, FUTEX_WAKE, 1);
	return cpu->reg[10].i;
      }
      else if (cpu->exceptions & ILLEGAL_INSTRUCTION) {
	fprintf(stderr, "core generated ILLEGAL_INSTRUCTION exception\n");
	GEN_SEGV;
      }
      else
	abort();
    }
  } /* while (1) */
}


void status_report()
{
  struct timeval *t1=&conf.start_tv, t2;
  gettimeofday(&t2, 0);
  double seconds = t2.tv_sec - t1->tv_sec;
  seconds += (t2.tv_usec - t1->tv_usec)/1e6;
  double total = 0;
  fprintf(stderr, "IPC");
  char delimitor = '=';
  for (int i=0; i<active_cores; i++) {
    struct core_t* cpu = &core[i];
    total += cpu->count.insn;
    double ipc = (double)cpu->count.insn / cpu->count.cycle;
    fprintf(stderr, "%c%5.3f", delimitor, ipc);
    delimitor = ',';
  }
  fprintf(stderr, " in %3.1fs for %3.1f MIPS\r", seconds, total/1e6/seconds);
}

void final_status()
{
  fprintf(stderr, "\nFinal Status\n");
  status_report();
  fprintf(stderr, "\n");
  for (int i=0; i<active_cores; i++)
    if (core[i].tid && conf.simulate) {
	print_cache(&core[i].icache, stderr);
	print_cache(&core[i].dcache, stderr);
      }
  fprintf(stderr, "\n\n");
}

#ifdef DEBUG
void print_pctrace(struct core_t* cpu)
{
  fprintf(stderr, "%score[%ld] last instructions\n", color(cpu->tid), cpu-core);
  for (int i=0; i<PCTRACEBUFSZ; i++) {
    Addr_t pc = cpu->debug.trace[cpu->debug.tb].pc;
    struct insn_t* p = insn(pc);
    int rd = p->op_rd;
    if (writeOp(p->op_code))
      rd = p->op.rs2;
    if (rd)
      fprintf(stderr, "[%016lx] ", cpu->debug.trace[cpu->debug.tb].regval.l);
    else
      fprintf(stderr, "[%16s] ", "");
    print_pc(pc, stderr);
    print_insn(pc, stderr);
    cpu->debug.tb = (cpu->debug.tb+1) & (PCTRACEBUFSZ-1);
  }
  fprintf(stderr, "%s", nocolor);
}

void print_callstack(struct core_t* cpu)
{
  fprintf(stderr, "%score[%ld] call stack\n", color(cpu->tid), cpu-core);
  for (int i=cpu->debug.cs-1; i>=0; i--) {
    struct callstack_t* csp = &cpu->debug.stack[i];
    fprintf(stderr, "%*s%08lx:", 2*i, "", csp->tgt);
    print_pc(csp->tgt, stderr);
    fprintf(stderr, " <- %08lx:", csp->ra);
    print_pc(csp->ra, stderr);
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "%s", nocolor);
}
#endif

