#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <limits.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "caveat.h"
#include "cache.h"

using namespace std;
void* operator new(size_t size);
void operator delete(void*) noexcept;

option<long> conf_Jump("jump",	2,		"Taken branch pipeline flush cycles");

option<int> conf_Imiss("imiss",	100,		"Instruction cache miss penalty");
option<int> conf_Iways("iways", 4,		"Instruction cache number of ways associativity");
option<int> conf_Iline("iline",	6,		"Instruction cache log-base-2 line size");
option<int> conf_Irows("irows",	6,		"Instruction cache log-base-2 number of rows");

option<int> conf_Dmiss("dmiss",	100,		"Data cache miss penalty");
option<int> conf_Dways("dways", 4,		"Data cache number of ways associativity");
option<int> conf_Dline("dline",	6,		"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	8,		"Data cache log-base-2 number of rows");

option<int> conf_Vmiss("vmiss",	100,		"Vector cache miss penalty");
option<int> conf_Vways("vways", 4,		"Vector cache number of ways associativity");
option<int> conf_Vline("vline",	10,		"Vector cache log-base-2 line size");
option<int> conf_Vrows("vrows",	4,		"Vector cache log-base-2 number of rows");

option<int> conf_cores("cores",	8,		"Maximum number of cores");

option<>    conf_perf( "perf",	"caveat",	"Name of shared memory segment");

class core_t : public hart_t {
  static volatile long global_time;
  long local_time;
public:
  cache_t dc;
  cache_t vc;
  core_t(core_t* from);
  core_t(int argc, const char* argv[], const char* envp[]);
  
  friend void simulator(hart_t* h, long pc, Insn_t* i, long count, long* addresses);
  friend void my_status(hart_t* h);
  
  //  core_t* newcore() { return new core_t(this); }
  //  void proxy_syscall(long sysnum);
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  long system_clock() { return global_time; }
  long local_clock() { return local_time; }
  void update_time();
  
  cache_t* dcache() { return &dc; }
  cache_t* vcache() { return &vc; }
  void print();
};


void simulator(hart_t* h, long pc, Insn_t* i, long count, long* a)
{
  core_t* p = (core_t*)h;
  for (; count>0; count--) {
    p->local_time++;
    uint64_t attr = attributes[i->opcode()];
    if (attr & ATTR_ld|ATTR_st) {
      if (attr & ATTR_vec) {
	if (!p->vc.lookup(*a++, (attr&ATTR_st)))
	  p->local_time += p->vc.penalty();
      }
      else
	p->dc.lookup(*a++, (attr&ATTR_st));
    }
    pc += i->compressed() ? 2 : 4;
    i +=  i->compressed() ? 1 : 2;
  }
}

void core_t::print()
{
  dc.print();
  vc.print();
}

volatile long core_t::global_time;

core_t::core_t(core_t* from)
  : hart_t(from),
    dc("Data",   conf_Dmiss, conf_Dways, conf_Dline, conf_Drows, true),
    vc("Vector", conf_Vmiss, conf_Vways, conf_Vline, conf_Vrows, true)
{
  local_time = 0;
}

core_t::core_t(int argc, const char* argv[], const char* envp[])
  : hart_t(argc, argv, envp),
    dc("Data",   conf_Dmiss, conf_Dways, conf_Dline, conf_Drows, true),
    vc("Vector", conf_Vmiss, conf_Vways, conf_Vline, conf_Vrows, true)
		 
{
  local_time = 0;
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


void start_time();
double elapse_time();

void my_status(hart_t* h)
{
  core_t* p = (core_t*)h;
  double N = p->executed();
  double M = p->dc.refs() + p->vc.refs();
  fprintf(stderr, "%4.2f(%3.1f%%:%5.3f%%,%3.1f%%:%5.3f%%)", N/p->local_clock(),
	  100.0*p->dc.refs()/M, 100.0*p->dc.misses()/N,
	  100.0*p->vc.refs()/M, 100.0*p->vc.misses()/N);
}

void status_report(statfunc_t my_status)
{
  double realtime = elapse_time();
  long total = hart_t::total_count();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  if (hart_t::threads() <= 16) {
    char separator = '(';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      my_status(p);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::threads() > 1)
    fprintf(stderr, "(%d cores)", hart_t::threads());
}

void exitfunc()
{
  fprintf(stderr, "\n--------\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%ld] ", p->tid());
    p->print();
  }
  fprintf(stderr, "\n");
  status_report(my_status);
  fprintf(stderr, "\n");
}

#ifdef DEBUG
void signal_handler(int nSIGnum)
{
  fprintf(stderr, "signal_handler");
  long my_tid = gettid();
  for (core_t* p=core_t::list(); p; p=p->next()) {
    if (p->tid() == my_tid) {
      p->debug.print();
      exit(-1);
    }
  }
  fprintf(stderr, "Cannot find tid=%ld\n", my_tid);
  exit(-2);
}
#endif

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "cachesim: RISC-V cache simulator");
  if (argc == 0)
    help_exit();
  start_time();
  core_t* mycpu = new core_t(argc, argv, envp);
  atexit(exitfunc);
  mycpu->interpreter(&simulator, &my_status);
}
