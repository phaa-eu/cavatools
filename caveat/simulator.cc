#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <limits.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "cache.h"
#include "../uspike/options.h"
#include "../uspike/uspike.h"
#include "../uspike/mmu.h"
#include "../uspike/cpu.h"

option<long> conf_Jump("jump",	2,	"Taken branch pipeline flush cycles");
option<int> conf_Dmiss("dmiss",	20,	"Data cache miss penalty");
option<int> conf_Dways("dways", 4,	"Data cache number of ways associativity");
option<int> conf_Dline("dline",	6,	"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	6,	"Data cache log-base-2 number of rows");

class mem_t : public mmu_t {
public:
  long local_time;
  long begin_pc;		// of sequence before current event
  mem_t();
  void timing_model(long event_pc);
  long jump_model(long npc, long pc) { timing_model(pc);                          local_time += conf_Jump;    return npc; }
  long load_model( long a,  long pc) { timing_model(pc); if (!dc.lookup(a      )) local_time += dc.penalty(); return a; }
  long store_model(long a,  long pc) { timing_model(pc); if (!dc.lookup(a, true)) local_time += dc.penalty(); return a; }
  void amo_model(  long a,  long pc) { timing_model(pc); if (!dc.lookup(a, true)) local_time += dc.penalty();           }
  cache_t* dcache() { return &dc; }
  long clock() { return local_time; }
  void print();
private:
  cache_t dc;
};

class core_t : public mem_t, public cpu_t {
  static volatile long global_time;
public:
  core_t(core_t* p);
  core_t(int argc, const char* argv[], const char* envp[]);
  core_t* newcore() { return new core_t(this); }
  void proxy_syscall(long sysnum);
  
  static core_t* list() { return (core_t*)cpu_t::list(); }
  core_t* next() { return (core_t*)cpu_t::next(); }
  mem_t* mem() { return static_cast<mem_t*>(this); }
  cache_t* dcache() { return mem()->dcache(); }

  long system_clock() { return global_time; }
  long local_clock() { return mem()->clock(); }
  void update_time();
};

volatile long core_t::global_time;

inline void mem_t::timing_model(long event_pc)
{
  while (begin_pc < event_pc) {
    local_time++;
    begin_pc += code.at(begin_pc).opcode() <= Last_Compressed_Opcode ? 2 : 4;
  }
}

mem_t::mem_t() : dc("Data cache", conf_Dmiss, conf_Dways, conf_Dline, conf_Drows, true)
{
  local_time = 0;
}

void mem_t::print()
{
  dc.print();
}

core_t::core_t(core_t* p) : cpu_t(p, mem())
{
  local_time = p->local_time;
  begin_pc = p->read_pc();
}

core_t::core_t(int argc, const char* argv[], const char* envp[]) : cpu_t(argc, argv, envp, mem())
{
  begin_pc = read_pc();
}


#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

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
  cpu_t::proxy_syscall(sysnum);
  /*
  global_time += SYSCALL_OVERHEAD;
  local_time = global_time;
  update_time();
  futex((int*)&global_time, FUTEX_WAKE, INT_MAX);
  */
}

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
  core_t* mycpu = new core_t(argc, argv, envp);
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
    fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS IPC", core_t::total_count(), realtime, core_t::total_count()/1e6/realtime);
    char separator = '=';
    for (core_t* p=core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%4.2f", separator, (double)p->count()/p->local_clock());
      separator = ',';
    }
  }
}
