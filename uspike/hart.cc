#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "hart.h"

volatile hart_t* hart_t::cpu_list =0;
volatile long hart_t::total_insns =0;
volatile int hart_t::num_threads =0;

hart_t* hart_t::find(int tid)
{
  for (hart_t* p=list(); p; p=p->link)
    if (p->my_tid == tid)
      return p;
  return 0;
}

void hart_t::incr_count(long n)
{
  _executed += n;
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
  hart_t* thisCPU = hart_t::find(gettid());
  thisCPU->debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}

#endif


hart_t::hart_t(hart_t* from)
{
  if (from)
    memcpy(this, from, sizeof(hart_t));
  else {
    memset(this, 0, sizeof(hart_t));
    pc = code.entry();
  }
  my_tid = gettid();
  _executed = 0;
  do {
    link = list();
  } while (!__sync_bool_compare_and_swap(&cpu_list, link, this));
  int old_n;			// atomically increment thread count
  do {
    old_n = num_threads;
  } while (!__sync_bool_compare_and_swap(&num_threads, old_n, old_n+1));
  _number = old_n;		// after loop in case of race
}

void hart_t::set_tid()
{
  my_tid = gettid();
}


#define CSR_FFLAGS	0x1
#define CSR_FRM		0x2
#define CSR_FCSR	0x3

long hart_t::get_csr(int what)
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

void hart_t::set_csr(int what, long val)
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
