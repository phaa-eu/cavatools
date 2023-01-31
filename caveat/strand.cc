#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/mman.h>

#include "options.h"
#include "caveat.h"
#include "instructions.h"
#include "strand.h"

extern "C" {
  long initialize_stack(int argc, const char** argv, const char** envp);
};

volatile hart_t* hart_t::cpu_list =0;
volatile int hart_t::num_threads =0;

hart_t* hart_t::find(int tid)
{
  for (volatile hart_t* p=list(); p; p=p->link)
    if (p->my_tid == tid)
      return (hart_t*)p;
  return 0;
}

void strand_t::print_trace(long pc, Insn_t* i)
{
  if (i->opcode() == Op_ZERO) {
    fprintf(stderr, "x");
    return;
  }
  fprintf(stderr, "\t%7ld ", executed());
  if (i->rd() == NOREG) {
    if (attributes[i->opcode()] & ATTR_st)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i->rs2()], xrf[i->rs2()]); 
    else if ((attributes[i->opcode()] & (ATTR_cj|ATTR_uj)) && (i->rs1() != NOREG))
      fprintf(stderr, "%4s[%016lx] ", reg_name[i->rs1()], xrf[i->rs1()]); 
    else if (attributes[i->opcode()] & ATTR_ex)
      fprintf(stderr, "%4s[%016lx] ", reg_name[10], xrf[10]);
    else
      fprintf(stderr, "%4s[%16s] ", "", "");
  }
  else
    fprintf(stderr, "%4s[%016lx] ", reg_name[i->rd()], xrf[i->rd()]);
  labelpc(pc);
  disasm(pc, i, "\n");
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

void Debug_t::insert(long c, long pc, Insn_t* i)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor].count = c;
  trace[cursor].pc    = pc;
  trace[cursor].i     = i;
  trace[cursor].val   = ~0l;
}

void Debug_t::addval(reg_t val)
{
  trace[cursor].val = val;
}

void Debug_t::print()
{
  for (int k=0; k<PCTRACEBUFSZ; k++) {
    pctrace_t t = get();
    Insn_t* i = t.i;
    if (i->rd() != NOREG)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[i->rd()], t.val);
    else if (attributes[i->opcode()] & ATTR_st)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[i->rs2()], t.val);
    else
      fprintf(stderr, "%15ld %4s[%16s] ", t.count, "", "");
    labelpc(t.pc);
    disasm(t.pc, i, "");
    fprintf(stderr, "\n");
  }
}

#endif

strand_t::strand_t(class hart_t* h, int argc, const char* argv[], const char* envp[])
{
  memset(this, 0, sizeof(strand_t));
  pc = loadelf(argv[0]);
  xrf[2] = initialize_stack(argc, argv, envp);
  hart = h;
  addresses = (long*)mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  next_report = 100;
}

strand_t::strand_t(class hart_t* h, strand_t* from)
{
  memcpy(this, from, sizeof(strand_t));
  hart = h;
  addresses = (long*)mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  _executed = 0;
  next_report = 100;
}


#define CSR_FFLAGS	0x1
#define CSR_FRM		0x2
#define CSR_FCSR	0x3

long strand_t::get_csr(int what)
{
  switch (what) {
  case CSR_FFLAGS:
    return fcsr.f.flags;
  case CSR_FRM:
    return fcsr.f.rm;
  case CSR_FCSR:
    return fcsr.ui;
  default:
    die("unsupported CSR number %d", what);
  }
}

void strand_t::set_csr(int what, long val)
{
  switch (what) {
  case CSR_FFLAGS:
    fcsr.f.flags = val;
    break;
  case CSR_FRM:
    fcsr.f.rm = val;
    break;
  case CSR_FCSR:
    fcsr.ui = val;
    break;
  default:
    die("unsupported CSR number %d", what);
  }
}





void hart_t::attach_hart()
{
  do {
    link = list();
  } while (!__sync_bool_compare_and_swap(&cpu_list, link, this));
  int old_n;			// atomically increment thread count
  do {
    old_n = num_threads;
  } while (!__sync_bool_compare_and_swap(&num_threads, old_n, old_n+1));
  _number = old_n;		// after loop in case of race
  my_tid = gettid();
}

hart_t::hart_t(hart_t* from)
{
  strand = new strand_t(this, from->strand);
  attach_hart();
}

hart_t::hart_t(int argc, const char* argv[], const char* envp[])
{
  strand = new strand_t(this, argc, argv, envp);
  attach_hart();
  //  long sp = initialize_stack(argc, argv, envp);
  //  strand->write_reg(2, sp);	// x2 is stack pointer
}

void hart_t::interpreter(simfunc_t f, statfunc_t s)	{ strand->interpreter(f, s); }
void hart_t::set_tid()		{ my_tid = gettid(); }
long hart_t::executed()		{ return strand->executed(); }

long hart_t::total_count()
{
  long total = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next())
    total += p->executed();
  return total;
}

/*
void hart_t::my_report(long total)
{
  fprintf(stderr, "%1ld%%", 100*executed()/total);
}
*/


void hart_t::print_debug_trace()
{
#ifdef DEBUG
  strand->debug.print();
#else
  die("DEBUG not enabled!");
#endif
}
