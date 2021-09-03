/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "interpreter.h"
#include "cpu.h"
#include "uspike.h"

#define THREAD_STACK_SIZE  (1<<14)

struct syscall_map_t {
  int sysnum;
  const char* name;
};

struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"  
};
const int highest_ecall_num = HIGHEST_ECALL_NUM;

void status_report()
{
  double realtime = elapse_time();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", cpu_t::total_count(), realtime, cpu_t::total_count()/1e6/realtime);
  if (cpu_t::threads() <= 16) {
    char separator = '(';
    for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%1ld%%", separator, 100*p->count()/cpu_t::total_count());
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (cpu_t::threads() > 1)
    fprintf(stderr, "(%d cores)", cpu_t::threads());
}

/* RISCV-V clone() system call arguments not same as X86_64:
   a0 = flags
   a1 = child_stack
   a2 = parent_tidptr
   a3 = tls
   a4 = child_tidptr
*/

static int thread_interpreter(void* arg)
{
  processor_t* p = (processor_t*)arg;
  WRITE_REG(2, READ_REG(11));	// a1 = child_stack
  WRITE_REG(4, READ_REG(13));	// a3 = tls
  WRITE_REG(10, 0);		// indicating we are child thread
  STATE.pc += 4;		// skip over ecall pc
  cpu_t* newcpu = new cpu_t(p);
  enum stop_reason reason;
  //conf.show = true;
  //  sleep(100);
  //fprintf(stderr, "starting thread interpreter, tid=%d, tp=%lx\n", gettid(), READ_REG(4));
  do {
    reason = interpreter(newcpu, 10000);
    //status_report();
  } while (reason == stop_normal);
  status_report();
  fprintf(stderr, "\n");
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  else if (reason != stop_exited)
    die("unknown reason %d", reason);
  return 0;
	      
}

cpu_t* initial_cpu(long entry, long sp)
{
  processor_t* p = new processor_t(conf.isa, "mu", conf.vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  STATE.pc = entry;
  WRITE_REG(2, sp);
  cpu_t* mycpu = new cpu_t(p);
  return mycpu;
}

cpu_t* cpu_t::find(int tid)
{
  for (cpu_t* p=cpu_list; p; p=p->link)
    if (p->my_tid == tid)
      return p;
  return 0;
}

void show(cpu_t* cpu, long pc, FILE* f)
{
  Insn_t i = code.at(pc);
  int rn = i.rd()==NOREG ? i.rs2() : i.rd();
  long rv = cpu->spike()->get_state()->XPR[rn];
  if (rn != NOREG)
    fprintf(stderr, "%6ld: %4s[%16lx] ", cpu->tid(), reg_name[rn], rv);
  else
    fprintf(stderr, "%6ld: %4s[%16s] ", cpu->tid(), "", "");
  labelpc(pc);
  disasm(pc);
}

template<class T> bool cmpswap(long pc, processor_t* p)
{
  Insn_t i = code.at(pc);
  T* ptr = (T*)READ_REG(i.rs1());
  T expect  = READ_REG(i.rs2());
  T replace = READ_REG(i.rs3());
  T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
  WRITE_REG(code.at(pc+4).rs1(), oldval);
  if (oldval == expect)  WRITE_REG(i.rd(), 0);	/* sc was successful */
  return oldval == expect;
}

#define imm i.immed()
#define wpc(e) pc=(e)
#define r1 xrf[i.rs1()]
#define r2 xrf[i.rs2()]
#define wrd(e) xrf[i.rd()]=(e)

enum stop_reason interpreter(cpu_t* cpu, long number)
{
  //fprintf(stderr, "interpreter()\n");
  processor_t* p = cpu->spike();
  long* xrf = (long*)&p->get_state()->XPR;
  
  enum stop_reason reason = stop_normal;
  long pc = STATE.pc;
  long count = 0;
#ifdef DEBUG
  long oldpc;
#endif
  while (count < number) {
#ifdef DEBUG
    dieif(!code.valid(pc), "Invalid PC %lx, oldpc=%lx", pc, oldpc);
    oldpc = pc;
    cpu->debug.insert(cpu->count()+count+1, pc);
#endif
  repeat_dispatch:
    Insn_t i = code.at(pc);
    switch (i.opcode()) {
    case Op_ZERO:
      code.set(pc, decoder(code.image(pc), pc));
      goto repeat_dispatch;
	
#include "fastops.h"
	
    default:
      try {
	pc = golden[i.opcode()](pc, cpu);
      } catch (trap_user_ecall& e) {
	if (proxy_ecall(p, cpu->count()+count)) {
	  reason = stop_exited;
	  goto early_stop;
	}
	pc += 4;
	if (conf.show) {
	  cpu->incr_count(1);
	  goto early_stop;
	}
      } catch (trap_breakpoint& e) {
	reason = stop_breakpoint;
	goto early_stop;
      }
      break;
    }
    xrf[0] = 0;
    ++count;
#ifdef DEBUG
    i = code.at(oldpc);
    int rn = i.rd()==NOREG ? i.rs2() : i.rd();
    cpu->debug.addval(i.rd(), cpu->spike()->get_state()->XPR[rn]);
    if (conf.show)
      show(cpu, oldpc);
#endif
  }
 early_stop:
  STATE.pc = pc;
  cpu->incr_count(count);
  return reason;
}
			  
long I_ZERO(long pc, cpu_t* cpu)    { die("I_ZERO should never be dispatched!"); }
long I_ILLEGAL(long pc, cpu_t* cpu) { die("I_ILLEGAL at 0x%lx", pc); }
long I_UNKNOWN(long pc, cpu_t* cpu) { die("I_UNKNOWN at 0x%lx", pc); }

void substitute_cas(long lo, long hi)
{
#ifndef NOFASTOPS
  // look for compare-and-swap pattern
  long possible=0, replaced=0;
  for (long pc=lo; pc<hi; pc+=code.at(pc).compressed()?2:4) {
    Insn_t i = code.at(pc);
    if (!(i.opcode() == Op_lr_w || i.opcode() == Op_lr_d))
      continue;
    possible++;
    Insn_t i2 = code.at(pc+4);
    if (i2.opcode() != Op_bne && i2.opcode() != Op_c_bnez) continue;
    int len = 4 + (i2.opcode()==Op_c_bnez ? 2 : 4);
    Insn_t i3 = code.at(pc+len);
    if (i3.opcode() != Op_sc_w && i3.opcode() != Op_sc_d) continue;
    // pattern found, check registers
    int load_reg = i.rd();
    int addr_reg = i.rs1();
    int test_reg = (i2.opcode() == Op_c_bnez) ? 0 : i2.rs2();
    int newv_reg = i3.rs2();
    int flag_reg = i3.rd();
    if (i2.rs1() != load_reg) continue;
    if (i3.rs1() != addr_reg) continue;
    // pattern is good
    Opcode_t op;
    if (len == 8) op = (i.opcode() == Op_lr_w) ? Op_cas12_w : Op_cas12_d;
    else          op = (i.opcode() == Op_lr_w) ? Op_cas10_w : Op_cas10_d;
    code.set(pc, reg3insn(op, flag_reg, addr_reg, test_reg, newv_reg));
    replaced++;
  }
  if (replaced != possible) {
    fprintf(stderr, "%ld Load-Reserve found, %ld substitution failed\n", possible, possible-replaced);
    exit(-1);
  }
#endif
}

#include "dispatch_table.h"
