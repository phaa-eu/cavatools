#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <limits.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "caveat.h"
#include "cachesim.h"

option<int> conf_Dmiss("dmiss",	50,		"Data cache miss penalty");
option<int> conf_Dways("dways", 4,		"Data cache number of ways associativity");
option<int> conf_Dline("dline",	6,		"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	8,		"Data cache log-base-2 number of rows");

option<int> conf_cores("cores",	8,		"Maximum number of cores");

option<bool> conf_quiet("quiet",	false, true,			"No status report");

long hart_t::total_count()
{
  long total = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next())
    total += p->executed();
  return total;
}

void status_report()
{
  double realtime = elapse_time();
  long total = hart_t::total_count();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS, IPC(D$)", total, realtime, total/1e6/realtime);
  if (hart_base_t::num_harts() <= 16) {
    char separator = '=';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      double N = p->executed();
      double M = p->dc->refs();
      fprintf(stderr, "%4.2f(%1.0f%%)", N/p->local_clock(), 100.0*p->dc->misses()/N);
      separator = ',';
    }
  }
  else if (hart_base_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_base_t::num_harts());
}

void dumb_simulator(hart_base_t* h, Header_t* bb)
{
  hart_t* p = (hart_t*)h;
  long* ap = p->addresses();
  Insn_t* i = insnp(bb);
  p->advance(bb->count);
  for (long k=0; k<bb->count; k++) {
    ++i;
    ATTR_bv_t attr = attributes[i->opcode()];
    if (attr & (ATTR_ld|ATTR_st)) {
      if (!p->dc->lookup(*ap++, (attr&ATTR_st)))
	p->advance(conf_Dmiss);
    }
  }
}

void view_simulator(hart_base_t* h, Header_t* bb)
{
  hart_t* p = (hart_t*)h;
  long* ap = p->addresses();
  // long pc = bb->addr;
  Insn_t* i = insnp(bb);
  uint64_t* c = &p->counters()[index(bb)];
  //  *c += bb->count;			// header counts number of executions
  *c += 1;
  p->advance(bb->count);
  for (long k=0; k<bb->count; k++) {
    ++i, ++c;
    ATTR_bv_t attr = attributes[i->opcode()];
    if (attr & (ATTR_ld|ATTR_st)) {
      if (!p->dc->lookup(*ap++, (attr&ATTR_st))) {
	*c += 1;
	p->advance(conf_Dmiss);
      }
    }
    // pc += i->compressed() ? 2 : 4;
  }
  if (p->more_insn(bb->count))
    status_report();
}

void hart_t::print()
{
  dc->print();
}

volatile long hart_t::global_time;

void hart_t::initialize()
{
  dc = new_cache("Data",   conf_Dways, conf_Dline, conf_Drows, true);
  local_time = 0;
  _executed = 0;
  next_report = conf_report;
}  

#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

#if 0
#define SYSCALL_OVERHEAD 100
void hart_t::proxy_syscall(long sysnum)
{
  /*
  update_time();
  long t = global_time;
  fprintf(stderr, "local=%ld %ld=global\n", local_time, t);
  while (t < local_time) {
    futex((int*)&global_time, FUTEX_WAIT, (int)t);
    t = global_time;
    fprintf(stderr, "local=%ld %ld=global\n", local_time, t);
  }
  local_time = LONG_MAX;
  */
  hart_base_t::proxy_syscall(sysnum);
  /*
  global_time += SYSCALL_OVERHEAD;
  local_time = global_time;
  update_time();
  futex((int*)&global_time, FUTEX_WAKE, INT_MAX);
  */
}
#endif

void hart_t::update_time()
{
  long last_local = LONG_MAX;
  for (hart_t* p=hart_t::list(); p; p=p->next()) {
    if (p->local_time < last_local)
      last_local = p->local_time;
  }
  dieif(last_local<global_time, "local %ld < %ld global", last_local, global_time);
  global_time = last_local;
}


#ifdef DEBUG
void signal_handler(int nSIGnum)
{
  fprintf(stderr, "signal_handler");
  long my_tid = gettid();
  for (hart_t* p=hart_t::list(); p; p=p->next()) {
    if (p->tid() == my_tid) {
      //      p->debug.print();
      exit(-1);
    }
  }
  fprintf(stderr, "Cannot find tid=%ld\n", my_tid);
  exit(-2);
}
#endif
