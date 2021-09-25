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

#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

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
option<long> conf_report("report", 10,		"Status report every N million instructions");

volatile long system_clock;

class mem_t : public mmu_t, public perf_t {
public:
  long last_pc;
  long cycles_run;
  mem_t(long n);
  void sync_system_clock();
  void icache_lookup(long pc);
  void insn_model(long end_pc);
  long jump_model(long npc, long pc);
  long load_model( long a,  long pc);
  long store_model(long a,  long pc);
  void amo_model(  long a,  long pc);
  cache_t* dcache() { return &dc; }
  long cycles() { return cycles_run; }
  void print();
private:
  cache_t ic;
  cache_t dc;
};

class core_t : public mem_t, public hart_t {
  long start_time;
  long stall_time;
  long saved_local_time;
  void init() { cycles_run=start_time=system_clock; stall_time=0; }
public:
  core_t(long entry);
  core_t(core_t* p);
  core_t* newcore() { return new core_t(this); }
  void proxy_syscall(long sysnum);
  void run_thread();
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }
  mem_t* mem() { return static_cast<mem_t*>(this); }
  cache_t* dcache() { return mem()->dcache(); }

  long local_clock() { return cycles_run==LONG_MAX ? saved_local_time : cycles_run; }
  long run_time() { return local_clock() - start_time; }
  long run_cycles() { return run_time() - stall_time; }
  static void status_report();
};


void update_time()
{
  long last_local = LONG_MAX;
  //char buffer[4096], *b=buffer;
  //b += sprintf(b, "cycles_run =");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    //b += sprintf(b, " %ld", p->cycles_run);
    if (p->cycles_run < last_local)
      last_local = p->cycles_run;
  }
  //b += sprintf(b, "\n");
  //fputs(buffer, stderr);
  if (last_local < LONG_MAX) {
    //__atomic_store(&global_time, &last_local, __ATOMIC_RELAXED);
    system_clock = last_local;
    futex((int*)&system_clock, FUTEX_WAKE, INT_MAX);
  }
}

void mem_t::sync_system_clock()
{
  update_time();
  // wait until system_time catches up to our local_time
  for (long t=system_clock; t<cycles_run; t=system_clock) {
    //fprintf(stderr, "Me at %ld, %ld=system_clock\n", cycles_run, system_clock);
    futex((int*)&system_clock, FUTEX_WAIT, (int)t);
  }
}

#define SYSCALL_OVERHEAD 100
void core_t::proxy_syscall(long sysnum)
{
  
#if 0
  char buf[4096], *b=buf;
  b += sprintf(b, "ecall[%ld] system=%ld local", tid(), system_clock());
  for (core_t* p=core_t::list(); p; p=p->next()) {
    if (p->local_clock() == LONG_MAX)
      b += sprintf(b, " stalled");
    else
      b += sprintf(b, " +%ld", p->local_clock()-system_clock());
  }
  b += sprintf(b, "\n");
  fputs(buf, stderr);
#endif

  update_time();
  sync_system_clock();
  long t0 = system_clock;
  saved_local_time = cycles_run;
  cycles_run = LONG_MAX;	// indicate we are stalled
  update_time();
  hart_t::proxy_syscall(sysnum);
  cycles_run = system_clock + SYSCALL_OVERHEAD;
  //cycles_run = system_clock;
  stall_time += cycles_run - t0; // accumulate stalled 
  update_time();
}



void core_t::status_report()
{
  long insns = total_count();
  double realtime = elapse_time();
  char buf[4096];
  char* b = buf;
  //b += sprintf(b, "\r\33[2K%12ld cycles %3.1fs %3.1f MIPS IPC(util)", system_clock(), realtime, insns/1e6/realtime);
  b += sprintf(b, "\r\33[2K%12ld cycles %3.1fs %3.1f MIPS IPC(util)", system_clock, realtime, insns/1e6/realtime);
  char separator = '=';
  for (core_t* p=core_t::list(); p; p=p->next()) {
    b += sprintf(b, "%c%4.2f", separator, (double)p->executed()/p->run_cycles());
    b += sprintf(b, "(%4.2f%%)", 100.0*p->run_cycles()/p->run_time());
    //b += sprintf(b, "%c%ld/%ld", separator, p->executed(), p->run_cycles());
    //b += sprintf(b, "(%ld/%ld)", p->run_cycles(), p->run_time());
    separator = ',';
  }
  fputs(buf, stderr);
}



inline void mem_t::icache_lookup(long pc)
{
  if (ic.lookup(pc))
    return;
  cycles_run += ic.penalty();
  inc_imiss(pc);
  inc_cycle(pc, ic.penalty());
  //update_time();
}

inline void mem_t::insn_model(long end_pc)
{
  long pc = last_pc;
  icache_lookup(pc);
  pc = ic.linemask(pc) + ic.linesize();
  while (pc <= end_pc) {
    icache_lookup(pc);
    pc += ic.linesize();
  }
  pc = last_pc;
  while (pc <= end_pc) {
    inc_count(pc);
    inc_cycle(pc);
    cycles_run += 1;
    pc += code.at(pc).bytes();
  }
  last_pc = pc;
}

inline long mem_t::jump_model(long npc, long pc)
{
  insn_model(pc);
  cycles_run += conf_Jump;
  inc_cycle(npc, conf_Jump);
  last_pc = npc;
  return npc;
}

inline long mem_t::load_model(long a, long pc)
{
  insn_model(pc);
  if (!dc.lookup(a)) {
    inc_dmiss(pc);
    cycles_run += dc.penalty();
    inc_cycle(pc, dc.penalty());
    //update_time();
  }
  return a;
}

inline long mem_t::store_model(long a, long pc)
{
  insn_model(pc);
  if (!dc.lookup(a, true)) {
    inc_dmiss(pc);
    cycles_run += dc.penalty();
    inc_cycle(pc, dc.penalty());
    //update_time();
  }
  return a;
}

inline void mem_t::amo_model(long a, long pc)
{
  sync_system_clock();
  insn_model(pc);
  if (!dc.lookup(a, true)) {
    inc_dmiss(pc);
    cycles_run += dc.penalty();
    inc_cycle(pc, dc.penalty());
    //update_time();
  }
}

mem_t::mem_t(long n)
  : perf_t(n),
    ic("Instruction", conf_Imiss, conf_Iways, conf_Iline, conf_Irows, false),
    dc("Data",        conf_Dmiss, conf_Dways, conf_Dline, conf_Drows, true)
		 
{
  cycles_run = 0;
}

void mem_t::print()
{
  ic.print();
  dc.print();
}

core_t::core_t(long entry) : hart_t(mem()), mem_t(number())
{
  init();
  last_pc = entry;
}

core_t::core_t(core_t* p) : hart_t(p, mem()), mem_t(number())
{
  init();
  last_pc = p->last_pc;
}

void core_t::run_thread()
{
  fprintf(stderr, "Running [%ld]\n", tid());
  while (1) {
    interpreter(conf_report*1000000);
    update_time();
    status_report();
  }
}
  
  



void start_time();
double elapse_time();

void exitfunc()
{
  fprintf(stderr, "\n--------\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%ld] ", p->tid());
    p->mem()->print();
  }
  fprintf(stderr, "\n");
  core_t::status_report();
  fprintf(stderr, "\ncaveat exited normally");
}

//#ifdef DEBUG
#if 0
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
  core_t* mycpu = new core_t(code.entry());
  mycpu->write_reg(2, sp);	// x2 is stack pointer
  
  atexit(exitfunc);

#if 0
  //#ifdef DEBUG
  static struct sigaction action;
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_handler = signal_handler;
  sigaction(SIGSEGV, &action, NULL);
#endif

  mycpu->run_thread();
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
