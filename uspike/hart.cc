#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
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

#include "spike_link.h"

long hart_t::read_reg(int n)
{
  processor_t* p = spike();
  return READ_REG(n);
}

void hart_t::write_reg(int n, long value)
{
  processor_t* p = spike();
  WRITE_REG(n, value);
}

long* hart_t::reg_file()
{
  processor_t* p = spike();
  //return (long*)&(p->get_state()->XPR);
  return (long*)&p->get_state()->XPR[0];
}

long hart_t::read_pc()
{
  processor_t* p = spike();
  return STATE.pc;
}

void hart_t::write_pc(long value)
{
  processor_t* p = spike();
  STATE.pc = value;
}

long* hart_t::ptr_pc()
{
  processor_t* p = spike();
  return (long*)&STATE.pc;
}

extern option<> conf_isa;
extern option<> conf_vec;

hart_t::hart_t(mmu_t* m)
{
  processor_t* p = new processor_t(conf_isa, "mu", conf_vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  STATE.pc = code.entry();
  my_tid = gettid();
  spike_cpu = p;
  caveat_mmu = m;
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

hart_t::hart_t(hart_t* from, mmu_t* m) : hart_t(m)
{
  memcpy(spike()->get_state(), from->spike()->get_state(), sizeof(state_t));
}

void hart_t::run_thread()
{
  extern option<long> conf_report;
  while (1) {
    interpreter(conf_report*1000000L);
    status_report();
  }
}

void hart_t::set_tid()
{
  my_tid = gettid();
}
