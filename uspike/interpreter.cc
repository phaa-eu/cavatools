/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include "interpreter.h"
#include "uspike.h"

thread_local processor_t* p;	// many Spike macros assume pointer is named p
const char* isa_string;
const char* vec_string;

static const struct {
  int sysnum;
  const char*name;
} rv_to_host[] = {
#include "ecall_nums.h"
};

static bool make_ecall(long executed)
{
  long rvnum = READ_REG(17);
  if (rvnum < 0 || rvnum >= rv_syscall_entries)
    throw trap_user_ecall();
  long sysnum = rv_to_host[rvnum].sysnum;
  if (sysnum == __NR_exit || sysnum == __NR_exit_group) {
    STATE.pc += 4;
    return true;
  }
  WRITE_REG(10, proxy_syscall(sysnum,
			      executed,
			      rv_to_host[rvnum].name,
			      READ_REG(10),
			      READ_REG(11),
			      READ_REG(12),
			      READ_REG(13),
			      READ_REG(14),
			      READ_REG(15)));
  STATE.pc += 4;		// increment after in case exception
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

void* init_cpu(long entry, long sp, const char* isa, const char* vec)
{
  isa_string = isa;
  vec_string = vec;
  p = create_cpu(isa_string, vec_string);
  STATE.pc = entry;
  WRITE_REG(2, sp);
  return p;
}

void* clone_cpu(long sp, long tp)
{
  processor_t* q = create_cpu(isa_string, vec_string);
  memcpy(q->get_state(), p->get_state(), sizeof(state_t));
  q->get_state()->XPR.write(2, sp);
  q->get_state()->XPR.write(4, tp);
  return q;
}

enum stop_reason single_step(long &executed)
{
  try {
    STATE.pc = golden[code.at(STATE.pc).op_code](STATE.pc, p);
    return stop_normal;
  } catch(trap_user_ecall& e) {
    if (make_ecall(executed)) {
      executed++;
      return stop_exited;
    }
    else {
      executed++;
      return stop_normal;
    }
  } catch(trap_breakpoint& e) {
    return stop_breakpoint;
  }
}

enum stop_reason run_insns(long number, long &executed)
{
  long count = 0;
  long pc = STATE.pc;
  enum stop_reason reason = stop_normal;
  while (count < number) {
    try {
      pc = golden[code.at(pc).op_code](pc, p);
    } catch(trap_user_ecall& e) {
      STATE.pc = pc;
      count += 1;
      if (make_ecall(executed)) {
	reason = stop_exited;
	break;
      }
    } catch(trap_breakpoint& e) {
      reason = stop_breakpoint;
      break;
    }
  }
  STATE.pc = pc;
  executed += count;
  return reason;
}

			  
long I_ZERO(long pc, processor_t* p)
{
  Insn_t i = decoder(pc);
  code.set(pc, i);
  return golden[i.op_code](pc, p);
}

long I_ILLEGAL(long pc, processor_t* p)
{
  dieif(1, "I_ILLEGAL at 0x%lx", pc);
}

long I_UNKNOWN(long pc, processor_t* p)
{
  dieif(1, "I_UNKNOWN at 0x%lx", pc);
}

#include "dispatch_table.h"
