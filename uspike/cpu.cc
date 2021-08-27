#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "cpu.h"
#include "uspike.h"

cpu_t* cpu_t::cpu_list =0;
long cpu_t::total_insns =0;
int cpu_t::num_threads =0;

cpu_t::cpu_t(processor_t* p)
{
  spike_cpu = p;
  my_tid = gettid();
  insn_count = 0;
  do {
    link = cpu_list;
  } while (!__sync_bool_compare_and_swap(&cpu_list, link, this));
  int old_n;
  do {
    old_n = num_threads;
  } while (!__sync_bool_compare_and_swap(&num_threads, old_n, old_n+1));
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

Insn_t longimm(Opcode_t code, int8_t rd, int32_t longimmed)
{
  Insn_t i(code);
  i.op_rd = rd;
  i.op_longimm = longimmed;
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
  if (conf.gdb) {
    HandleException(nSIGnum);
    ProcessGdbCommand();
  }
  else
    cpu_t::find(gettid())->debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}

#endif
