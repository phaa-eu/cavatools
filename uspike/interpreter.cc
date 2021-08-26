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
  long total = 0;
  double realtime = elapse_time();
  int n = 0;
  for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
    total += p->insn_count;
    n++;
  }
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  if (n <= 16) {
    char separator = '(';
    for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%1ld%%", separator, 100*p->insn_count/total);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (n > 1)
    fprintf(stderr, "(%d cores)", n);
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
    reason = interpreter(newcpu, conf.stat*1000000);
    status_report();
  } while (reason == stop_normal);
  status_report();
  fprintf(stderr, "\n");
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  else if (reason != stop_exited)
    die("unknown reason %d", reason);
  return 0;
	      
}

static bool proxy_ecall(processor_t* p, long executed)
{
  static long ecall_count;
  long rvnum = READ_REG(17);
  if (rvnum<0 || rvnum>highest_ecall_num || !rv_to_host[rvnum].name) {
    fprintf(stderr, "Illegal ecall number %ld\n", rvnum);
    abort();
  }
  long sysnum = rv_to_host[rvnum].sysnum;
  long a0=READ_REG(10), a1=READ_REG(11), a2=READ_REG(12), a3=READ_REG(13), a4=READ_REG(14), a5=READ_REG(15);
  const char* name = rv_to_host[rvnum].name;
  ecall_count++;
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "\n%12ld %8lx: ecalls=%ld %s:%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)",
	    executed, STATE.pc, ecall_count, name, sysnum, a0, a1, a2, a3, a4, a5);
  switch (sysnum) {
  case SYS_exit:
  case SYS_exit_group:
    return true;
#if 0
  case SYS_brk:
    {
      brk_lock.lock();
      long rv = emulate_brk(a0);
      brk_lock.unlock();
      return rv;
    }
#endif
  case SYS_clone:
    {
      //fprintf(stderr, "\nclone() called, tid=%d\n", gettid());
      processor_t* q = new processor_t(conf.isa, "mu", conf.vec, 0, 0, false, stdout);
      memcpy(q->get_state(), p->get_state(), sizeof(state_t));
      //memcpy(q, p, sizeof(processor_t));
      char* interp_stack = new char[THREAD_STACK_SIZE];
      interp_stack += THREAD_STACK_SIZE; // grows down
      long child_tid = proxy_clone(thread_interpreter, interp_stack, a0, q, (void*)a2, (void*)a4);
      WRITE_REG(10, (long)child_tid);
      //sleep(3);
      //fprintf(stderr, "returning from ecall, a0=%ld\n", READ_REG(10));
      //conf.show = true;
      goto finished;
    }
#if 0
  case SYS_futex:
    {
      timespec nap = { 0, 1000 }; // sleep for this many nanoseconds initially
      long counter = 1;
      long rc = proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5);
      while (rc == -1 && errno == EAGAIN && --counter >= 0) {
	nanosleep(&nap, 0);
	nap.tv_nsec *= 2;
	rc = proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5);
      }
      if (rc == -1)
	perror("Proxy futex");
      WRITE_REG(10, rc);
      goto finished;
    }
#endif
  }
  WRITE_REG(10, proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5));
 finished:
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "->0x%lx", READ_REG(10));
  return false;
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
  return oldval != expect;
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
  while (count < number) {
    do {
      
#if 0
      int v;
      if ((v=tid_alone) && (v!=cpu->tid())) {
	fprintf(stderr, "tid_alone=%d, my_tid=%ld waiting\n", v, cpu->tid());
	while ((v=tid_alone) && (v!=cpu->tid())) {
	  syscall(SYS_futex, (int*)&tid_alone, FUTEX_WAIT, v, 0, 0, 0);
	  fprintf(stderr, "still waiting\n");
	}
      }
#endif
      
#ifdef DEBUG
      long oldpc = pc;
      cpu->debug.insert(cpu->insn_count+count+1, pc);
#endif
      if (!code.valid(pc))
	diesegv();
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
	  //if (proxy_ecall(p, cpu->insn_count+count)) {
	  if (proxy_ecall(p, cpu->insn_count)) {
	    reason = stop_exited;
	    goto early_stop;
	  }
	  pc += 4;
	  if (conf.show) {
	    //count++;
	    cpu->insn_count++;
	    goto early_stop;
	  }
	} catch (trap_breakpoint& e) {
	  reason = stop_breakpoint;
	  goto early_stop;
	}
	break;
      }
      xrf[0] = 0;
      
#ifdef DEBUG
      i = code.at(oldpc);
      int rn = i.rd()==NOREG ? i.rs2() : i.rd();
      cpu->debug.addval(i.rd(), cpu->spike()->get_state()->XPR[rn]);
      if (conf.show)
	show(cpu, oldpc);
#endif

    } while (++count < number);
  }
 early_stop:
  STATE.pc = pc;
  cpu->insn_count += count;
  return reason;
}
			  
long I_ZERO(long pc, cpu_t* cpu)    { die("I_ZERO should never be dispatched!"); }
long I_ILLEGAL(long pc, cpu_t* cpu) { die("I_ILLEGAL at 0x%lx", pc); }
long I_UNKNOWN(long pc, cpu_t* cpu) { die("I_UNKNOWN at 0x%lx", pc); }

//long I_ecall(long pc, cpu_t* cpu)   { die("I_ecall should never be dispatched!"); }
//long I_ebreak(long pc, cpu_t* cpu)   { die("I_ebreak should never be dispatched!"); }
//long I_c_ebreak(long pc, cpu_t* cpu)   { die("I_c_ebreak should never be dispatched!"); }

#include "dispatch_table.h"