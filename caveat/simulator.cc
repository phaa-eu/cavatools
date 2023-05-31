#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <limits.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"
#include "cache.h"
#include "perf.h"

using namespace std;
void* operator new(size_t size);
void operator delete(void*) noexcept;

option<long> conf_Jump("jump",	2,		"Taken branch pipeline flush cycles");

option<int> conf_Imiss("imiss",	15,		"Instruction cache miss penalty");
option<int> conf_Iways("iways", 4,		"Instruction cache number of ways associativity");
option<int> conf_Iline("iline",	6,		"Instruction cache log-base-2 line size");
option<int> conf_Irows("irows",	6,		"Instruction cache log-base-2 number of rows");

option<int> conf_Dmiss("dmiss",	15,		"Data cache miss penalty");
option<int> conf_Dways("dways", 4,		"Data cache number of ways associativity");
option<int> conf_Dline("dline",	6,		"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	6,		"Data cache log-base-2 number of rows");
option<int> conf_cores("cores",	8,		"Maximum number of cores");

option<>    conf_perf( "perf",	"caveat",	"Name of shared memory segment");

class mem_t : public mmu_t, public perf_t {
public:
  long local_time;
  mem_t(long n);
  void insn_model(long pc);
  long jump_model(long npc, long pc);
  long load_model( long a,  long pc);
  long store_model(long a,  long pc);
  void amo_model(  long a,  long pc);
  cache_t* dcache() { return dc; }
  long clock() { return local_time; }
  void print();
private:
  cache_t* ic;
  cache_t* dc;
};

inline void mem_t::insn_model(long pc)
{
  if (!ic->lookup(pc)) {
    local_time += conf_Imiss;
    inc_imiss(pc);
    inc_cycle(pc, conf_Imiss);
  }
  inc_count(pc);
  inc_cycle(pc);
  local_time += 1;
}

inline long mem_t::jump_model(long npc, long pc)
{
  local_time += conf_Jump;
  inc_cycle(npc, conf_Jump);
  return npc;
}

inline long mem_t::load_model(long a, long pc)
{
  if (!dc->lookup(a)) {
    inc_dmiss(pc);
    local_time += conf_Dmiss;
    inc_cycle(pc, conf_Dmiss);
  }
  return a;
}

inline long mem_t::store_model(long a, long pc)
{
  if (!dc->lookup(a, true)) {
    inc_dmiss(pc);
    local_time += conf_Dmiss;
    inc_cycle(pc, conf_Dmiss);
  }
  return a;
}

inline void mem_t::amo_model(long a, long pc)
{
  if (!dc->lookup(a, true)) {
    inc_dmiss(pc);
    local_time += conf_Dmiss;
    inc_cycle(pc, conf_Dmiss);
  }
}

class core_t : public mem_t, public hart_t {
  static volatile long global_time;
public:
  core_t();
  core_t(core_t* p);
  core_t* newcore() { return new core_t(this); }
  void proxy_syscall(long sysnum);
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }
  mem_t* mem() { return static_cast<mem_t*>(this); }
  cache_t* dcache() { return mem()->dcache(); }

  long system_clock() { return global_time; }
  long local_clock() { return mem()->clock(); }
  void update_time();
};

volatile long core_t::global_time;

mem_t::mem_t(long n)
  : perf_t(n)
{
  local_time = 0;
  ic = new_cache("Instruction", conf_Iways, conf_Iline, conf_Irows, false);
  dc = new_cache("Data",        conf_Dways, conf_Dline, conf_Drows, true);
}

void mem_t::print()
{
  ic->print();
  dc->print();
}

core_t::core_t() : hart_t(mem()), mem_t(number())
{
}

core_t::core_t(core_t* p) : hart_t(p, mem()), mem_t(number())
{
  local_time = p->local_time;
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
  hart_t::proxy_syscall(sysnum);
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
  code.loadelf(argv[0]);
  perf_t::create(code.base(), code.limit(), conf_cores, conf_perf);
  for (int i=0; i<perf_t::cores(); i++)
    new perf_t(i);
  long sp = initialize_stack(argc, argv, envp);
  core_t* mycpu = new core_t();
  mycpu->write_reg(2, sp);	// x2 is stack pointer
  
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
    mycpu->interpreter(10000000L);
    double realtime = elapse_time();
    fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS IPC", core_t::total_count(), realtime, core_t::total_count()/1e6/realtime);
    char separator = '=';
    for (core_t* p=core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%4.2f", separator, (double)p->executed()/p->local_clock());
      separator = ',';
    }
  }
}

extern "C" {

#define poolsize  (1<<30)	/* size of simulation memory pool */

static char simpool[poolsize];	/* base of memory pool */
static volatile char* pooltop = simpool; /* current allocation address */

void *malloc(size_t size)
{
  char volatile *rv, *newtop;
  do {
    volatile char* after = pooltop + size + 16; /* allow for alignment */
    if (after > simpool+poolsize) {
      fprintf(stderr, " failed\n");
      return 0;
    }
    rv = pooltop;
    newtop = (char*)((unsigned long)after & ~0xfL); /* always align to 16 bytes */
  } while (!__sync_bool_compare_and_swap(&pooltop, rv, newtop));
      
  return (void*)rv;
}

void free(void *ptr)
{
  /* we don't free stuff */
}

void *calloc(size_t nmemb, size_t size)
{
  return malloc(nmemb * size);
}

void *realloc(void *ptr, size_t size)
{
  return 0;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
  return 0;
}

};
