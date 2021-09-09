#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>

#include "cache.h"
#include "../uspike/options.h"
#include "../uspike/mmu.h"
#include "../uspike/cpu.h"

option<int> conf_Dmiss("dmiss",	20,	"Data cache miss penalty");
option<int> conf_Dways("dways", 4,	"Data cache number of ways associativity");
option<int> conf_Dline("dline",	6,	"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	6,	"Data cache log-base-2 number of rows");

class mem_t : public mmu_t {
  cache_t dc;
public:
  mem_t();
  long load_model( long a, int b) { dc.lookup(a);       return a; }
  long store_model(long a, int b) { dc.lookup(a, true); return a; }
  void amo_model(  long a, int b) { dc.lookup(a, true); }
  cache_t* dcache() { return &dc; }
  void print();
};

class core_t : public cpu_t {
  static volatile long global_time;
  long local_time;
public:
  core_t(core_t* p, mem_t* m);
  core_t(int argc, const char* argv[], const char* envp[], mem_t* m);
  core_t* newcore() { return new core_t(this, new mem_t); }
  static core_t* list() { return (core_t*)cpu_t::list(); }
  core_t* next() { return (core_t*)cpu_t::next(); }
  mem_t* mem() { return (mem_t*)mmu(); }
  cache_t* dcache() { return mem()->dcache(); }
  void before_syscall(long num);
  void after_syscall();
};

mem_t::mem_t() : dc("Data cache", conf_Dmiss, conf_Dways, conf_Dline, conf_Drows, true)
{
}

void mem_t::print()
{
  dc.print();
}

core_t::core_t(core_t* p, mem_t* m) : cpu_t(p, m)
{
}

core_t::core_t(int argc, const char* argv[], const char* envp[], mem_t* m) : cpu_t(argc, argv, envp, m)
{
}

void core_t::before_syscall(long num)
{
}

void core_t::after_syscall()
{
}

void start_time();
double elapse_time();
void interpreter(cpu_t* cpu);
void status_report();

void exitfunc()
{
  fprintf(stderr, "\n--------\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%ld] ", p->tid());
    p->mem()->print();
  }
  fprintf(stderr, "\n");
  status_report();
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
  parse_options(argc, argv, "caveat: user-mode RISC-V parallel simulator");
  if (argc == 0)
    help_exit();
  start_time();
  core_t* mycpu = new core_t(argc, argv, envp, new mem_t);
  atexit(exitfunc);

#ifdef DEBUG
  static struct sigaction action;
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_handler = signal_handler;
  sigaction(SIGSEGV, &action, NULL);
#endif
  
  while (1) {
    mycpu->run_epoch(10000000L);
    double realtime = elapse_time();
    fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS D$", core_t::total_count(), realtime, core_t::total_count()/1e6/realtime);
    char separator = '=';
    for (core_t* p=core_t::list(); p; p=p->next()) {
      mem_t* mm = (mem_t*)p->mmu();
      fprintf(stderr, "%c%4.2f%%", separator, 100.0*p->dcache()->misses()/p->count());
      separator = ',';
    }
  }
}
