/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include "interpreter.h"
#include "uspike.h"

thread_local processor_t* p;	// many Spike macros assume pointer is named p

long get_pc()
{
  return STATE.pc;
}

long get_reg(int rn)
{
  return STATE.XPR[rn];
}

bool proxy_ecall(long executed)
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
  if (sysnum == 60 || sysnum == 231) // X64 exit || exit_group
    return true;
  WRITE_REG(10, proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5));
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "->0x%lx", READ_REG(10));
  return false;
}

static processor_t* create_cpu(const char* isa, const char* vec)
{
  processor_t* p = new processor_t(isa, "mu", vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  return p;
}

void* init_cpu(long entry, long sp)
{
  p = create_cpu(conf.isa, conf.vec);
  STATE.pc = entry;
  WRITE_REG(2, sp);
  return p;
}

void* clone_cpu(long sp, long tp)
{
  processor_t* q = create_cpu(conf.isa, conf.vec);
  memcpy(q->get_state(), p->get_state(), sizeof(state_t));
  q->get_state()->XPR.write(2, sp);
  q->get_state()->XPR.write(4, tp);
  return q;
}

enum stop_reason interpreter(long number, long &executed)
{
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
	if (proxy_ecall(executed)) {
	  reason = stop_exited;
	  goto early_stop;
	}
	pc += 4;
	break;
      case Op_ebreak:
      case Op_c_ebreak:
	reason = stop_breakpoint;
	goto early_stop;
	
      default:
	try {
	  pc = golden[i.op_code](pc, p);
	} catch (trap_user_ecall& e) {
	  if (proxy_ecall(executed)) {
	    reason = stop_exited;
	    goto early_stop;
	  }
	  pc += 4;
	} catch (trap_breakpoint& e) {
	  reason = stop_breakpoint;
	  goto early_stop;
	}
	break;
      }
#ifdef DEBUG
      i = code.at(oldpc);
      int rn = i.op_rd==NOREG ? i.op.r2 : i.op_rd;
      debug.addval(i.op_rd, get_reg(rn));
#endif
      WRITE_REG(0, 0);
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
