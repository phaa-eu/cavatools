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

option<int> conf_Vmiss("vmiss",	100,		"Vector cache miss penalty");
option<int> conf_Vways("vways", 4,		"Vector cache number of ways associativity");
option<int> conf_Vline("vline",	10,		"Vector cache log-base-2 line size");
option<int> conf_Vrows("vrows",	4,		"Vector cache log-base-2 number of rows");

option<int> conf_cores("cores",	8,		"Maximum number of cores");

option<long> conf_report("report", 100000000, "Status report frequency");
option<bool> conf_quiet("quiet",	false, true,			"No status report");

long core_t::total_count()
{
  long total = 0;
  for (core_t* p=core_t::list(); p; p=p->next())
    total += p->executed();
  return total;
}

void status_report()
{
  double realtime = elapse_time();
  long total = core_t::total_count();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS, IPC(D$,V$)", total, realtime, total/1e6/realtime);
  if (hart_t::num_harts() <= 16) {
    char separator = '=';
    for (core_t* p=core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      double N = p->executed();
      double M = p->dc->refs() + p->vc->refs();
      fprintf(stderr, "%4.2f(%1.0f%%:%4.2f%%,%1.0f%%:%4.2f%%)", N/p->local_clock(),
	      100.0*p->dc->refs()/M, 100.0*p->dc->misses()/N,
	      100.0*p->vc->refs()/M, 100.0*p->vc->misses()/N);
      separator = ',';
    }
  }
  else if (hart_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_t::num_harts());
}

void simulator(hart_t* h, Header_t* bb)
{
  core_t* p = (core_t*)h;
  long* ap = p->addresses();
  // long pc = bb->addr;
  Insn_t* i = insnp(bb);
  uint64_t* c = &p->counters()[p->index(bb)];
  //  *c += bb->count;			// header counts number of executions
  *c += 1;
  p->local_time += bb->count;
  for (long k=0; k<bb->count; k++) {
    ++i, ++c;
    ATTR_bv_t attr = attributes[i->opcode()];
    if (attr & (ATTR_ld|ATTR_st)) {
      if (attr & ATTR_vec) {
	if (!p->vc->lookup(*ap++, (attr&ATTR_st))) {
	  *c += 1;
	  p->local_time += conf_Vmiss;
	}
      }
      else {
	if (!p->dc->lookup(*ap++, (attr&ATTR_st))) {
	  *c += 1;
	  p->local_time += conf_Dmiss;
	}
      }
    }
    // pc += i->compressed() ? 2 : 4;
  }
  if (p->more_insn(bb->count) > p->next_report) {
    //    status_report();
    p->next_report += conf_report;
  }
}

void core_t::print()
{
  dc->print();
  vc->print();
}

volatile long core_t::global_time;

void core_t::initialize()
{
  dc = new_cache("Data",   conf_Dways, conf_Dline, conf_Drows, true);
  vc = new_cache("Vector", conf_Vways, conf_Vline, conf_Vrows, true); 
  local_time = 0;
  _executed = 0;
  next_report = conf_report;
}  

#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

#if 0
#define SYSCALL_OVERHEAD 100
void core_t::proxy_syscall(long sysnum)
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
  hart_t::proxy_syscall(sysnum);
  /*
  global_time += SYSCALL_OVERHEAD;
  local_time = global_time;
  update_time();
  futex((int*)&global_time, FUTEX_WAKE, INT_MAX);
  */
}
#endif

void core_t::update_time()
{
  long last_local = LONG_MAX;
  for (core_t* p=core_t::list(); p; p=p->next()) {
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
  for (core_t* p=core_t::list(); p; p=p->next()) {
    if (p->tid() == my_tid) {
      //      p->debug.print();
      exit(-1);
    }
  }
  fprintf(stderr, "Cannot find tid=%ld\n", my_tid);
  exit(-2);
}
#endif
