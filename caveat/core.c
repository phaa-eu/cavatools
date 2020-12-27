/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "caveat.h"
#include "opcodes.h"
#include "caveat_fp.h"
#include "arith.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"


unsigned long lrsc_set = 0;	/* global atomic lock */
long regval[tr_memq_len];	/* space for maximum number of instructions */

void init_core( struct core_t* cpu, long start_tick, const struct timeval* start_timeval )
{
  memset(cpu, 0, sizeof(struct core_t));
  for (int i=32; i<64; i++)	/* initialize FP registers to boxed float 0 */
    cpu->reg[i].ul = 0xffffffff00000000UL;
  cpu->counter.start_tick = start_tick;
  cpu->counter.start_timeval = *start_timeval;
}


int outer_loop( struct core_t* cpu )
{
  if (cpu->params.breakpoint)
    insert_breakpoint(cpu->params.breakpoint);
  int fast_mode = 1;
  cpu->holding_pc = 0;
  while (1) { // terminated by program making exit() ecall
    long next_report = (cpu->counter.insn_executed+cpu->params.report) / cpu->params.report;
    next_report = next_report*cpu->params.report - cpu->counter.insn_executed;
    if (fast_mode)
      fast_sim(cpu, next_report);
    else
      slow_sim(cpu, next_report);
    
    switch (cpu->state.mcause) {
    case 0:  // max_count instructions executed
      status_report(cpu, stderr);
      continue;  // do not emulate instruction
      
    case 8:  // Environment call from U-mode
      if (proxy_ecall(cpu)) goto program_called_exit;
      break;
    case 14:  // CSR action
      proxy_csr(cpu, insn(cpu->pc), cpu->state.mtval);
      break;
      
    case 3:  /* Breakpoint */
      if (fast_mode) {
	if (--cpu->params.after > 0 || /* not ready to trace yet */
	    --cpu->params.skip > 0) {  /* only trace every n call */
	  cpu->holding_pc = 0L;	/* do not include current pc */
	  cpu->params.skip = cpu->params.every;
	  /* put instruction back and single step */
	  decode_instruction(insn(cpu->pc), cpu->pc);
	  cpu->state.mcause = 0;
	  fast_sim(cpu, 1);
	  /* reinserting breakpoint at subroutine entry */	  
	  insert_breakpoint(cpu->params.breakpoint);
	}
	else { /* insert breakpoint at subroutine return */
	  if (cpu->reg[RA].a)	/* _start called with RA==0 */
	    insert_breakpoint(cpu->reg[RA].a);
	  fast_mode = 0;		/* start tracing */
	  fifo_put(cpu->tb, trP(cpu->params.flags, 0, cpu->pc));
	}
      }
      else {  /* reinserting breakpoint at subroutine entry */
	insert_breakpoint(cpu->params.breakpoint);
	fast_mode = 1;		/* stop tracing */
	cpu->holding_pc = 0L;	/* do not include current pc */
      }
      cpu->state.mcause = 0;
      decode_instruction(insn(cpu->pc), cpu->pc);
      continue;  // re-execute at same pc

      // The following cases do not fall out
    case 2:  // Illegal instruction
      fprintf(stderr, "Illegal instruction at 0x%08lx\n", cpu->pc);
      GEN_SEGV;
    case 10:  // Unknown instruction
      fprintf(stderr, "Unknown instruction at 0x%08lx\n", cpu->pc);
      GEN_SEGV;
    default:  // Oh oh
      abort();
    }
    cpu->state.mcause = 0;
    cpu->pc += shortOp(insn(cpu->pc)->op_code) ? 2 : 4;
    cpu->counter.insn_executed++;
  }

 program_called_exit:
  cpu->counter.insn_executed++;	// don't forget to count last ecall
  if (!cpu->params.quiet) {
    clock_t end_tick = clock();
    double elapse_time = (end_tick - cpu->counter.start_tick)/CLOCKS_PER_SEC;
    double mips = cpu->counter.insn_executed / (1e6*elapse_time);
    fprintf(stderr, "\n\nExecuted %ld instructions in %3.1f seconds for %3.1f MIPS\n",
	      cpu->counter.insn_executed, elapse_time, mips);
  }
  return cpu->reg[10].i;
}


void status_report( struct core_t* cpu, FILE* f )
{
  if (cpu->params.quiet)
    return;
#if 0
  clock_t end_tick = clock();
  double elapse_time = (end_tick - cpu->counter.start_tick)/CLOCKS_PER_SEC;
#endif
  struct timeval *t1=&cpu->counter.start_timeval, t2;
  gettimeofday(&t2, 0);
  
  double msec = (t2.tv_sec - t1->tv_sec)*1000;
  msec += (t2.tv_usec - t1->tv_usec)/1000.0;
  double mips = cpu->counter.insn_executed / (1e3*msec);
  if (cpu->counter.insn_executed < 1000000000)
    fprintf(f, "\rExecuted %ld instructions in %3.1f milliseconds for %3.1f MIPS",
	    cpu->counter.insn_executed, msec, mips);
  else {
    double minutes = floor(msec/1e3 / 60.0);
    double seconds = msec/1e3 - 60.0*minutes;
    if (minutes > 0.0)
      fprintf(f, "\rExecuted %3.1f billion instructions in %3.0f minutes %3.0f seconds for %3.1f MIPS",
	      cpu->counter.insn_executed/1e9, minutes, seconds, mips);
    else
      fprintf(f, "\rExecuted %3.1f billion instructions in %3.1f seconds for %3.1f MIPS",
	      cpu->counter.insn_executed/1e9, msec/1e3, mips);
  }
}
