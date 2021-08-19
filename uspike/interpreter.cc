/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>

#include "interpreter.h"
#include "uspike.h"

#define THREAD_STACK_SIZE  (1<<14)

long get_pc(void* mycpu)
{
  processor_t* p = (processor_t*)mycpu;
  return STATE.pc;
}

long get_reg(void* mycpu, int rn)
{
  processor_t* p = (processor_t*)mycpu;
  return STATE.XPR[rn];
}

void show_insn(long pc)
{
  fprintf(stderr, "\r");
  labelpc(pc);
  disasm(pc);
}

void status_report(long insn_count)
{
  double realtime = elapse_time();
  fprintf(stderr, "\r%12ld insns %3.1fs %3.1f MIPS", insn_count, realtime, insn_count/1e6/realtime);
}

static int thread_interpreter(void* arg)
{
  processor_t* p = (processor_t*)arg;
  fprintf(stderr, "in thread_interpreter(), tid=%d\n", gettid());
  processor_t* newcpu = new processor_t(conf.isa, "mu", conf.vec, 0, 0, false, stdout);
  memcpy(newcpu, p, sizeof(processor_t));
  long child_stack = READ_REG(11); // from parent p
  long tls	   = READ_REG(13); // arguments to ecall clone()
  newcpu->get_state()->XPR.write(2, child_stack);
  newcpu->get_state()->XPR.write(4, tls);
  
  //sleep(2);
  p = newcpu;			// forget parent forever
  STATE.pc += 4;		// skip over ecall pc
  WRITE_REG(10, 0);		// indicating we are child thread
  enum stop_reason reason;
  long insn_count = 0;
  conf.show = true;
  do {
    reason = interpreter(newcpu, conf.stat*1000000, insn_count);
    status_report(insn_count);
  } while (reason == stop_normal);
  status_report(insn_count);
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
  case 60:			// X86_64 SYS_exit
  case 231:			// X86_64 SYS_exit_group
    return true;
  case 56: 			// X86_64 SYS_clone
    fprintf(stderr, "\nclone() called, tid=%d\n", gettid());
    { // RISCV-V clone() system call arguments not same as X86_64:
      long flags	= READ_REG(10);
      //long child_stack= READ_REG(11);
      long parent_tidptr= READ_REG(12);
      //long tls	= READ_REG(13);
      long child_tidptr	= READ_REG(14);
      char* newcpu_sp = new char[THREAD_STACK_SIZE] + THREAD_STACK_SIZE;
      //      void* newcpu = clone_cpu(p, child_stack, tls);
      flags &= ~CLONE_SETTLS;	// not implementing TLS in interpreter yet
      WRITE_REG(10, clone(thread_interpreter, newcpu_sp, flags, p,
			  parent_tidptr, 0, child_tidptr));
      sleep(100);
      fprintf(stderr, "returning from ecall, a0=%ld\n", READ_REG(10));
      conf.show = true;
      return false;
    }
  }
  WRITE_REG(10, proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5));
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "->0x%lx", READ_REG(10));
  return false;
}

void* initial_cpu(long entry, long sp)
{
  processor_t* p = new processor_t(conf.isa, "mu", conf.vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  STATE.pc = entry;
  WRITE_REG(2, sp);
  return p;
}

enum stop_reason interpreter(void* mycpu, long number, long &executed)
{
  processor_t* p = (processor_t*)mycpu;
  enum stop_reason reason = stop_normal;
  long count = 0;
  long pc = STATE.pc;
  while (count < number) {
    do {
#ifdef DEBUG
      long oldpc = pc;
      debug.insert(executed+count+1, pc);
#endif
      if (!code.valid(pc))
	diesegv();
    repeat_dispatch:
      Insn_t i = code.at(pc);
      switch (i.op_code) {
      case Op_ZERO:
	code.set(pc, decoder(code.image(pc), pc));
	goto repeat_dispatch;
	
#include "fastops.h"
	
      case Op_ecall:
	STATE.pc=pc;
	if (proxy_ecall(p, executed)) {
	  reason = stop_exited;
	  goto early_stop;
	}
	pc += 4;
	if (conf.show)
	  goto early_stop;
	break;
      case Op_ebreak:
      case Op_c_ebreak:
	reason = stop_breakpoint;
	goto early_stop;
	
      default:
	if (conf.show)
	  show_insn(pc);
	try {
	  pc = golden[i.op_code](pc, p);
	} catch (trap_user_ecall& e) {
	  if (proxy_ecall(p, executed)) {
	    reason = stop_exited;
	    goto early_stop;
	  }
	  pc += 4;
	  if (conf.show) {
	    count++;
	    goto early_stop;
	  }
	} catch (trap_breakpoint& e) {
	  reason = stop_breakpoint;
	  goto early_stop;
	}
	break;
      }
#ifdef DEBUG
      i = code.at(oldpc);
      int rn = i.op_rd==NOREG ? i.op.rs2 : i.op_rd;
      debug.addval(i.op_rd, get_reg(mycpu, rn));
#endif
      //p->get_state()->XPR[0]=0
      
    } while (++count < number);
  }
 early_stop:
  STATE.pc = pc;
  executed += count;
  return reason;
}
			  
long I_ZERO(long pc, processor_t* p)    { die("I_ZERO should never be dispatched!"); }
long I_ILLEGAL(long pc, processor_t* p) { die("I_ILLEGAL at 0x%lx", pc); }
long I_UNKNOWN(long pc, processor_t* p) { die("I_UNKNOWN at 0x%lx", pc); }

//long I_ecall(long pc, processor_t* p)   { die("I_ecall should never be dispatched!"); }
//long I_ebreak(long pc, processor_t* p)   { die("I_ebreak should never be dispatched!"); }
//long I_c_ebreak(long pc, processor_t* p)   { die("I_c_ebreak should never be dispatched!"); }

#include "dispatch_table.h"
