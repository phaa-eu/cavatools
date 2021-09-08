#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "mmu.h"
#include "cpu.h"

cpu_t* cpu_t::cpu_list =0;
long cpu_t::total_insns =0;
int cpu_t::num_threads =0;

cpu_t* cpu_t::find(int tid)
{
  for (cpu_t* p=cpu_list; p; p=p->link)
    if (p->my_tid == tid)
      return p;
  return 0;
}

Insn_t reg1insn (Opcode_t code, int8_t rd, int8_t rs1)
{
  Insn_t i(code);
  i.op_rd  = rd;
  i.op_rs1 = rs1;
  return i;
}

Insn_t reg2insn (Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2)
{
  Insn_t i(code);
  i.op_rd  = rd;
  i.op_rs1 = rs1;
  i.op.rs2 = rs2;
  return i;
}

Insn_t reg3insn (Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3)
{
  Insn_t i(code);
  i.op_rd  = rd;
  i.op_rs1 = rs1;
  i.op.rs2 = rs2;
  i.op.rs3 = rs3;
  return i;
}

Insn_t reg0imm(Opcode_t code, int8_t rd, int32_t longimmed)
{
  Insn_t i(code);
  i.op_rd = rd;
  i.op_longimm = longimmed;
  return i;
}

Insn_t reg1imm(Opcode_t code, int8_t rd, int8_t rs1, int16_t imm)
{
  Insn_t i(code, rd, imm);
  i.op_rs1 = rs1;
  return i;
}

Insn_t reg2imm(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm)
{
  Insn_t i(code, rd, imm);
  i.op_rs1 = rs1;
  i.op.rs2 = rs2;
  return i;
}

void cpu_t::incr_count(long n)
{
  insn_count += n;
  long oldtotal;
  do {
    oldtotal = total_insns;
  } while (!__sync_bool_compare_and_swap(&total_insns, oldtotal, oldtotal+n));
}

#ifdef DEBUG

pctrace_t Debug_t::get()
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  return trace[cursor];
}

void Debug_t::insert(pctrace_t pt)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor] = pt;
}

void Debug_t::insert(long c, long pc)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor].count = c;
  trace[cursor].pc    = pc;
  trace[cursor].val   = ~0l;
  trace[cursor].rn    = GPREG;
}

void Debug_t::addval(int rn, long val)
{
  trace[cursor].rn    = rn;
  trace[cursor].val   = val;
}

void Debug_t::print()
{
  for (int i=0; i<PCTRACEBUFSZ; i++) {
    pctrace_t t = get();
    if (t.rn != NOREG)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[t.rn], t.val);
    else
      fprintf(stderr, "%15ld %4s[%16s] ", t.count, "", "");
    labelpc(t.pc);
    if (code.valid(t.pc))
      disasm(t.pc, "");
    fprintf(stderr, "\n");
  }
}

void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
  //  ucontext_t* context = (ucontext_t*)vcontext;
  //  context->uc_mcontext.gregs[]
  fprintf(stderr, "\n\nsignal_handler(%d)\n", nSIGnum);
  cpu_t* thisCPU = cpu_t::find(gettid());
  thisCPU->debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}

#endif

#include "spike_link.h"

long cpu_t::read_reg(int n)
{
  processor_t* p = spike();
  return READ_REG(n);
}

void cpu_t::write_reg(int n, long value)
{
  processor_t* p = spike();
  WRITE_REG(n, value);
}

long* cpu_t::reg_file()
{
  processor_t* p = spike();
  //return (long*)&(p->get_state()->XPR);
  return (long*)&p->get_state()->XPR[0];
}

long cpu_t::read_pc()
{
  processor_t* p = spike();
  return STATE.pc;
}

void cpu_t::write_pc(long value)
{
  processor_t* p = spike();
  STATE.pc = value;
}

long* cpu_t::ptr_pc()
{
  processor_t* p = spike();
  return (long*)&STATE.pc;
}

cpu_t::cpu_t()
{
  my_tid = gettid();
  spike_cpu = 0;
  insn_count = 0;
  do {
    link = cpu_list;
  } while (!__sync_bool_compare_and_swap(&cpu_list, link, this));
  int old_n;
  do {
    old_n = num_threads;
  } while (!__sync_bool_compare_and_swap(&num_threads, old_n, old_n+1));
}

cpu_t::cpu_t(cpu_t* from, mmu_t* m) : cpu_t()
{
  processor_t* p = new processor_t(conf_isa, "mu", conf_vec, 0, 0, false, stdout);
  memcpy(p->get_state(), from->spike()->get_state(), sizeof(state_t));
  spike_cpu = p;
  caveat_mmu = m;
}

#include "elf_loader.h"

cpu_t::cpu_t(int argc, const char* argv[], const char* envp[], mmu_t* m) : cpu_t()
{
  long entry = load_elf_binary(argv[0], 1);
  code.init(low_bound, high_bound);
  long sp = initialize_stack(argc, argv, envp);
  processor_t* p = new processor_t(conf_isa, "mu", conf_vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  STATE.pc = entry;
  WRITE_REG(2, sp);
  spike_cpu = p;
  caveat_mmu = m;
}
