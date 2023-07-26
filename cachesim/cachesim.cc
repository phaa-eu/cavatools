#include <limits.h>
#include <unistd.h>
#include <pthread.h>

#include "caveat.h"
#include "hart.h"
#include "cache.h"

option<int> conf_Dways("dways", 4,		"Data cache number of ways associativity");
option<int> conf_Dline("dline",	6,		"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	8,		"Data cache log-base-2 number of rows");
option<int> conf_Dmiss("dmiss",	50,		"Data cache miss penalty");

option<long> conf_report("report", 1, "Status report per second");

class core_t : public hart_t {
  
  static volatile long global_time;
  long local_time;
  
  void initialize() {
    dc = new_cache("Data",   conf_Dways(), conf_Dline(), conf_Drows(), true);
    local_time = global_time;
  }
  
public:
  cache_t* dc;
  
  core_t(hart_t* from) :hart_t(from) { initialize(); }
  core_t(int argc, const char* argv[], const char* envp[]) :hart_t(argc, argv, envp) { initialize(); }
  
  void addtime(long delta) { local_time+=delta; }
  long local_clock() { return local_time; }
  
  long system_clock() { return global_time; }
  void update_time();
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }
};

volatile long core_t::global_time;

void simulator(hart_t* h, Header_t* bb, uintptr_t* ap)
{
  core_t* core = (core_t*)h;
  const Insn_t* i = insnp(bb+1);
  core->addtime(bb->count);
  for (long k=0; k<bb->count; k++, i++) {
    ATTR_bv_t attr = attributes[i->opcode()];
    if (attr & (ATTR_ld|ATTR_st)) {
      if (!core->dc->lookup(*ap++, (attr&ATTR_st)!=0))
	core->addtime(conf_Dmiss());
    }
  }
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

int clone_proxy(class hart_t* h)
{
  core_t* child = new core_t(h);
  return clone_thread(child);
}

void status_report()
{
  double realtime = elapse_time();
  long total = 0;
  for (core_t* p= core_t::list(); p; p=p->next()) {
    total += p->executed();
  }
  static double last_time;
  static long last_total;
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs MIPS(%3.1f,%3.1f), IPC(D$)", total, realtime,
	  (total-last_total)/1e6/(realtime-last_time), total/1e6/realtime);
  if (hart_t::num_harts() <= 16) {
    char separator = '=';
    for (core_t* p=core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      long N = p->executed();
      long M = p->dc->refs();
      fprintf(stderr, "%4.2f(%1.0f%%)", (double)N/p->local_clock(), 100.0*p->dc->misses()/N);
      separator = ',';
    }
  }
  else if (hart_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_t::num_harts());
  last_total = total;
  last_time = realtime;
}

void exitfunc()
{
  status_report();
  fprintf(stderr, "\n--------\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%d] ", p->tid());
    p->dc->print();
  }
  fprintf(stderr, "\n");
  status_report();
  fprintf(stderr, "\n");
}

void* status_thread(void* arg)
{
  while (1) {
    usleep(1000000/conf_report());
    status_report();
  }
}



int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "cachesim: RISC-V cache simulator");
  if (argc == 0)
    help_exit();

  core_t* cpu = new core_t(argc, argv, envp);
  cpu->simulator = simulator;
  cpu->clone = clone_proxy;
  atexit(exitfunc);

  if (conf_report() > 0) {
    pthread_t tnum;
    dieif(pthread_create(&tnum, 0, status_thread, 0), "failed to launch status_report thread");
  }
  
  start_time();
  cpu->interpreter();
}
