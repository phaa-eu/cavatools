/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"
#include "spike_link.h"

#define THREAD_STACK_SIZE  (1<<14)

option<>     conf_isa("isa",		"rv64imafdcv",			"RISC-V ISA string");
option<>     conf_vec("vec",		"vlen:128,elen:64,slen:128",	"Vector unit parameters");
option<bool> conf_ecall("ecall",	false, true,			"Show system calls");
option<bool> conf_quiet("quiet",	false, true,			"No status report");
option<bool> conf_show("show",		false, true,			"Trace execution");

struct syscall_map_t {
  int sysnum;
  const char* name;
};

struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"  
};
const int highest_ecall_num = HIGHEST_ECALL_NUM;

/* RISCV-V clone() system call arguments not same as X86_64:
   a0 = flags
   a1 = child_stack
   a2 = parent_tidptr
   a3 = tls
   a4 = child_tidptr
*/

void show(hart_t* cpu, long pc, FILE* f)
{
  Insn_t i = code.at(pc);
  int rn = i.rd()==NOREG ? i.rs2() : i.rd();
  long rv = cpu->read_reg(rn);
  if (rn != NOREG)
    fprintf(stderr, "%6d: %4s[%16lx] ", cpu->tid(), reg_name[rn], rv);
  else
    fprintf(stderr, "%6d: %4s[%16s] ", cpu->tid(), "", "");
  labelpc(pc);
  disasm(pc);
}

template<class T> bool hart_t::cas(long pc)
{
  Insn_t i = code.at(pc);
  T* ptr = (T*)read_reg(i.rs1());
  T expect  = read_reg(i.rs2());
  T replace = read_reg(i.rs3());
  T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
  write_reg(code.at(pc+4).rs1(), oldval);
  if (oldval == expect)  write_reg(i.rd(), 0);	/* sc was successful */
  return oldval == expect;
}

extern long (*golden[])(long pc, mmu_t& MMU, class processor_t* p);

bool hart_t::single_step()
{
  long pc = read_pc();
  Insn_t i = code.at(pc);
  try {
    write_pc(golden[i.opcode()](read_pc(), *mmu(), spike()));
  } catch (trap_breakpoint& e) {
    return true;
  }
  _executed++;
  return false;
}

long hart_t::interpreter(long& jpc)
{
  processor_t* p = spike();
  long pc = STATE.pc;
  long* xpr = (long*)&STATE.XPR[0];
  long count = 0;
  
  while (1) {
    long oldpc = pc;
#ifdef DEBUG
    dieif(!code.valid(pc), "Invalid PC %lx, oldpc=%lx", pc, oldpc);
    //debug.insert(executed()+1, pc);
    debug.insert(xpr[2], pc);
#endif

#undef MMU
#define MMU	(*mmu())
#define wrd(e)	xpr[i.rd()]=(e)
#define r1	xpr[i.rs1()]
#define r2	xpr[i.rs2()]
#define imm	i.immed()
#define jumped	{ jpc=oldpc; STATE.pc=pc; xpr[0]=0; _executed += ++count; if (conf_show) show(this, oldpc); return count; }
#define cas32op(n) pc += !cas<int32_t>(pc) ? code.at(pc+4).immed()+4 : n
#define cas64op(n) pc += !cas<int64_t>(pc) ? code.at(pc+4).immed()+4 : n
      
    Insn_t i = code.at(pc);
    switch (i.opcode()) {

#include "fastops.h"

    case Op_cas12_w:  cas32op(12); break;
    case Op_cas12_d:  cas64op(12); break;
    case Op_cas10_w:  cas32op(10); break;
    case Op_cas10_d:  cas64op(10); break;
      
    default:
      try {
	pc = golden[i.opcode()](pc, *mmu(), spike());
      } catch (trap_user_ecall& e) {
	STATE.pc = pc;
	proxy_ecall();
	pc += 4;
      } catch (trap_supervisor_ecall& e) { // set_pc() called, ie. jumped
	die("ugg");
      } catch (trap_breakpoint& e) {
	STATE.pc = pc;
	_executed += count;
	return count;		// didn't execute instruction
      }
    } // switch (i.opcode())
    xpr[0] = 0;
    ++count;
    if (conf_show)
      show(this, oldpc);
#ifdef DEBUG
    i = code.at(oldpc);
    int rn = i.rd()!=NOREG ? i.rd() : i.rs2()!=NOREG ? i.rs2() : i.rs1();
    debug.addval(rn, read_reg(rn));
#endif
  }
}

#ifdef DEBUG
void dump_trace_handler(int nSIGnum)
{
  fprintf(stderr, "Killed by signal %d\n", nSIGnum);
  hart_t* mycpu = hart_t::find(gettid());
  mycpu->debug.print();
  exit(-1);
}
#endif

#undef MMU

long I_ZERO(long pc, mmu_t& MMU, hart_t* cpu)    { die("I_ZERO should never be dispatched!"); return 0; }
long I_ILLEGAL(long pc, mmu_t& MMU, hart_t* cpu) { die("I_ILLEGAL at 0x%lx", pc); return 0; }
long I_UNKNOWN(long pc, mmu_t& MMU, hart_t* cpu) { die("I_UNKNOWN at 0x%lx", pc); return 0; }

#include "dispatch_table.h"
