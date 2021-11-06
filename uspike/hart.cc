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

hart_t* hart_t::find(int tid)
{
  for (hart_t* p=list(); p; p=p->link)
    if (p->my_tid == tid)
      return p;
  return 0;
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
    char buf[1024], *b=buf;
    const char* cs;
    switch (gettid() % 6) {
    case 0: cs = "31"; break;
    case 1: cs = "32"; break;
    case 2: cs = "33"; break;
    case 3: cs = "34"; break;
    case 4: cs = "35"; break;
    case 5: cs = "36"; break;
    }
    b += sprintf(b, "\e[%s;40m", cs);
    /*
    if (t.rn != NOREG)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[t.rn], t.val);
    else
      fprintf(stderr, "%15ld %4s[%16s] ", t.count, "", "");
    */
    //fprintf(stderr, "%16lx ", t.count);

    b += sprintf(b, "tid=%6d ", gettid());
    if (t.rn != NOREG)
      b += sprintf(b, "%4s[%016lx] ", reg_name[t.rn], t.val);
    else
      b += sprintf(b, "%4s[%16s] ", "", "");
    b += slabelpc(b, t.pc);
    if (code.valid(t.pc))
      b += sdisasm(b, t.pc);
    b += sprintf(b, "\e[0m\n");
    fputs(buf, stderr);
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

void* hart_t::freg_file()
{
  processor_t* p = spike();
  return (void*)&p->get_state()->FPR[0];
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

hart_t::hart_t()
{
  processor_t* p = new processor_t(conf_isa, "mu", conf_vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  STATE.pc = code.entry();
  last_event = STATE.pc;
  my_tid = gettid();
  spike_cpu = p;
  _executed = 0;
  // atomically add this to head of list
  do {
    link = list();
  } while (!__sync_bool_compare_and_swap(&cpu_list, link, this));
}

void hart_t::copy_state(hart_t* h)
{
  memcpy(spike()->get_state(), h->spike()->get_state(), sizeof(state_t));
  last_event = read_pc();
}

void hart_t::set_tid()
{
  my_tid = gettid();
}
