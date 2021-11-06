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
option<bool> conf_show("show",		false, true, 			"Trace execution");

void ( *const insn_model)(void* mmu,           long pc) = [](void* mmu,           long pc) {             };
long ( *const jump_model)(void* mmu, long npc, long pc) = [](void* mmu, long npc, long pc) { return npc; };
long ( *const load_model)(void* mmu,   long a, long pc) = [](void* mmu,   long a, long pc) { return a;   };
long (*const store_model)(void* mmu,   long a, long pc) = [](void* mmu,   long a, long pc) { return a;   };
void (  *const amo_model)(void* mmu,   long a, long pc) = [](void* mmu,   long a, long pc) {             };

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
    fprintf(stderr, "%6ld: %4s[%16lx] ", cpu->tid(), reg_name[rn], rv);
  else
    fprintf(stderr, "%6ld: %4s[%16s] ", cpu->tid(), "", "");
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

void hart_t::interpreter()
{
  processor_t* p = spike();
  long* xpr = reg_file();
  freg_t* fpr = (freg_t*)freg_file();
  long pc = read_pc();
  while (1) {
    long oldpc = pc;

#define wrd(e)	xpr[i.rd()]=(e)
#define r1	xpr[i.rs1()]
#define r2	xpr[i.rs2()]
#define imm	i.immed()
#define MMU	(*mmu())
#define wpc(npc)  pc=MMU.jump_model(npc, pc)
      
    Insn_t i = code.at(pc);
    switch (i.opcode()) {

#include "fastops.h"

    case Op_cas12_w:
      if (!cas<int32_t>(pc)) {
	wpc(pc+code.at(pc+4).immed()+4);
	break;
      }
      pc += 12;
      break;

    case Op_cas12_d:
      if (!cas<int64_t>(pc)) {
	wpc(pc+code.at(pc+4).immed()+4);
	break;
      }
      pc += 12;
      break;

    case Op_cas10_w:
      if (!cas<int32_t>(pc)) {
	wpc(pc+code.at(pc+4).immed()+4);
	break;
      }
      pc += 10;
      break;

    case Op_cas10_d:
      if (!cas<int64_t>(pc)) {
	wpc(pc+code.at(pc+4).immed()+4);
	break;
      }
      pc += 10;
      break;
      
    default:
      try {
	pc = golden[i.opcode()](pc, *mmu(), spike());
      } catch (trap_user_ecall& e) {
	write_pc(pc);
	proxy_ecall();
	pc += 4;
      } catch (trap_breakpoint& e) {
	write_pc(pc);
	return;
      }
    } // switch (i.opcode())
    xpr[0] = 0;
    _executed++;
    if (conf_show)
      show(this, oldpc);
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
