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
#include "mmu.h"
#include "cpu.h"
#include "spike_link.h"

#define THREAD_STACK_SIZE  (1<<14)



option<>     conf_isa("isa",		"rv64imafdcv",			"RISC-V ISA string");
option<>     conf_vec("vec",		"vlen:128,elen:64,slen:128",	"Vector unit parameters");
option<long> conf_stat("stat",		100,				"Status every M instructions");
option<bool> conf_ecall("ecall",	false, true,			"Show system calls");
option<bool> conf_quiet("quiet",	false, true,			"No status report");
option<long> conf_show("show",		0, 				"Trace execution after N gdb continue");
option<>     conf_gdb("gdb",		0, "localhost:1234", 		"Remote GDB on socket");


insnSpace_t code;

void* operator new(size_t size)
{
  //  fprintf(stderr, "operator new(%ld)\n", size);
  extern void* malloc(size_t);
  return malloc(size);
}
void operator delete(void*) noexcept
{
}


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
  if (conf_quiet)
    return;
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

void show(cpu_t* cpu, long pc, FILE* f)
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

template<class T> bool cpu_t::cas(long pc)
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

#define wrd(e)	xpr[i.rd()]=(e)
#define r1	xpr[i.rs1()]
#define r2	xpr[i.rs2()]
#define imm	i.immed()
#define MMU	(*mmu())
#define wpc(npc)  pc=MMU.jump_model(npc, pc)

bool cpu_t::single_step()
{
  processor_t* p = spike();
  long* xpr = reg_file();
  long pc = read_pc();
  Insn_t i = code.at(pc);
  long insns = 0;
  switch (i.opcode()) {
#include "fastops.h"
  default:
    try {
      pc = golden[i.opcode()](pc, *mmu(), spike());
    } catch (trap_breakpoint& e) {
      return true;
    }
    return false;
  } // switch (i.opcode())
  xpr[0] = 0;
  write_pc(pc);
  incr_count(1);
  return true;
}

bool cpu_t::run_epoch(long how_many)
{
  processor_t* p = spike();
  long* xpr = reg_file();
  long pc = read_pc();
  long insns = 0;
#ifdef DEBUG
  long oldpc;
#endif
  do {
#ifdef DEBUG
    dieif(!code.valid(pc), "Invalid PC %lx, oldpc=%lx", pc, oldpc);
    oldpc = pc;
    debug.insert(count()+insns+1, pc);
#endif
    Insn_t i = code.at(pc);
    switch (i.opcode()) {
#include "fastops.h"
    default:
      try {
	pc = golden[i.opcode()](pc, *mmu(), spike());
      } catch (trap_breakpoint& e) {
	write_pc(pc);
	incr_count(insns);
	return true;
      }
    } // switch (i.opcode())
    xpr[0] = 0;
#ifdef DEBUG
    i = code.at(oldpc);
    int rn = i.rd()==NOREG ? i.rs2() : i.rd();
    debug.addval(i.rd(), read_reg(rn));
    if (conf_show)
      show(this, oldpc);
#endif
  } while (++insns < how_many);
  write_pc(pc);
  incr_count(insns);
  return false;
}
  

void interpreter(cpu_t* cpu)
{
  while (1) {
    if (cpu->run_epoch(conf_stat*1000000L))
      return;
    status_report();
  }
}

#undef MMU
long I_ZERO(long pc, mmu_t& MMU, cpu_t* cpu)    { die("I_ZERO should never be dispatched!"); }
long I_ILLEGAL(long pc, mmu_t& MMU, cpu_t* cpu) { die("I_ILLEGAL at 0x%lx", pc); }
long I_UNKNOWN(long pc, mmu_t& MMU, cpu_t* cpu) { die("I_UNKNOWN at 0x%lx", pc); }

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
