/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <asm/unistd_64.h>

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

long get_pc()
{
  return STATE.pc;
}

long get_reg(int rn)
{
  return STATE.XPR[rn];
}

static bool make_ecall(long executed)
{
  static long ecall_count;
  long rvnum = READ_REG(17);
  if (rvnum < 0 || rvnum >= rv_syscall_entries)
    throw trap_user_ecall();
  long sysnum = rv_to_host[rvnum].sysnum;
  long a0=READ_REG(10), a1=READ_REG(11), a2=READ_REG(12), a3=READ_REG(13), a4=READ_REG(14), a5=READ_REG(15);
  const char* name = rv_to_host[rvnum].name;
  ecall_count++;
  if (report_ecalls && ecall_count % report_ecalls == 0)
    fprintf(stderr, "\n%12ld %8lx: ecalls=%ld %s:%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)",
	    executed, STATE.pc, ecall_count, name, sysnum, a0, a1, a2, a3, a4, a5);
  if (sysnum == 60 || sysnum == 231) { // X64 exit || exit_group
    STATE.pc += 4;
    return true;
  }
  WRITE_REG(10, proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5));
  STATE.pc += 4;		// increment after in case exception
  if (report_ecalls && ecall_count % report_ecalls == 0)
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
  enum stop_reason reason = stop_normal;
  try {
#ifdef DEBUG
    long oldpc = STATE.pc;
    debug.insert(executed+1, STATE.pc);
#endif
    STATE.pc = golden[code.at(STATE.pc).op_code](STATE.pc, p);
    WRITE_REG(0, 0);
    executed++;
#ifdef DEBUG
    Insn_t i = code.at(oldpc);
    int rn = i.op_rd==NOREG ? i.op.r2 : i.op_rd;
    debug.addval(rn, READ_REG(rn));
#endif
  } catch(trap_user_ecall& e) {
    if (make_ecall(executed))
      reason = stop_exited;
    executed++;
  } catch(trap_breakpoint& e) {
    reason = stop_breakpoint;
  }
  return stop_normal;
}

enum stop_reason run_insns(long number, long &executed)
{
  long count = 0;
  long pc = STATE.pc;
  enum stop_reason reason = stop_normal;
  while (count < number) {
    try {
      do {
#ifdef DEBUG
	long oldpc = pc;
	debug.insert(executed+count+1, pc);
#endif
	if (!code.valid(pc))
	  diesegv();
	pc = golden[code.at(pc).op_code](pc, p);
	WRITE_REG(0, 0);
#ifdef DEBUG
	Insn_t i = code.at(oldpc);
	//int rn = i.op_rd==NOREG ? i.op.r2 : i.op_rd;
	//debug.addval(rn, READ_REG(rn));
	debug.addval(i.op_rd, get_reg(i.op_rd));
#endif
      } while (++count < number);
    } catch(trap_user_ecall& e) {
      STATE.pc = pc;
      count++;
      if (!make_ecall(executed+count+1)) {
	debug.addval(10, get_reg(10));
	pc += 4;
	continue;
      }
      STATE.pc += 4;
      reason = stop_exited;
      goto early_exit;
    } catch(trap_breakpoint& e) {
      reason = stop_breakpoint;
      goto early_exit;
    }
  }
 early_exit:
  STATE.pc = pc;
  executed += count;
  return reason;
}

			  
long I_ZERO(long pc, processor_t* p)
{
  Insn_t i = decoder(code.image(pc), pc);
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
