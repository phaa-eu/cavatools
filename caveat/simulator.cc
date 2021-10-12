#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"
#include "../cache/cache.h"
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
option<long> conf_report("report", 10,		"Status report every N million instructions");


volatile long system_clock;

struct adrseg_t {
  long base;
  long limit;
  unsigned allowed;
  adrseg_t() { }
  adrseg_t(long b, long l, unsigned a) { base=b; limit=l; allowed=a; }
};

#define NUM_ADRSEG  4
adrseg_t adrseg[NUM_ADRSEG];
int adrsegs;



class multicache_t : public cache_t {
public:
  multicache_t(const char* name, int ways, int line, int rows, bool writeable, bool prefetch =false)
    : cache_t(name, ways, line, rows, writeable, prefetch) { }
  lru_fsm_t* modify_way(lru_fsm_t* p, long addr, long miss_ready, bool &prefetch) {
    int i = adrsegs;
    do {
      if (--i < 0) die("did not find segment");
    } while (!(adrseg[i].base <= addr && addr < adrseg[i].limit));
    if (i == 0)
      prefetch = false;
    while (!((1<<p->way) & adrseg[i].allowed))
      p--;
    return p;
  }
};

class mem_t : public mmu_t, public perf_t {
public:
  long last_pc;
  volatile long now;
  mem_t(long n);
  void sync_system_clock();

  void check_cache(long a, long pc, bool iswrite);
  long load_model( long a,  long pc);
  long store_model(long a,  long pc);
  void amo_model(  long a,  long pc);
 public:
  void insn_model(          long pc);
  long jump_model(long npc, long pc);

  void print();

  cache_t ic;
  cache_t dc;
  multicache_t mc;
};

class core_t : public mem_t, public hart_t {
  long start_time;
  long stall_time;
  long saved_local_time;
  static volatile long number_of_cores;
  void init() { now=start_time=system_clock; stall_time=0; }
public:
  core_t(long entry);
  core_t(core_t* p);
  core_t* newcore() { return new core_t(this); }
  void proxy_syscall(long sysnum);
  void run_thread();

  void custom(long pc);
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }
  mem_t* mem() { return static_cast<mem_t*>(this); }

  bool stalled() { return now == LONG_MAX; }
  long cycles() { return stalled() ? saved_local_time : now; }
  long run_time() { return cycles() - start_time; }
  long run_cycles() { return run_time() - stall_time; }
};

void core_t::custom(long pc)
{
  Insn_t i = code.at(pc);
  dieif(i.opcode() != Op_adrseg, "expecting Op_adrseg");
  if (i.rs1()==0 && i.rs2()==0) {      // reset
    adrseg[0] = adrseg_t(LONG_MIN, LONG_MAX, ~0);
    adrsegs = 1;
  }
  else if (adrsegs < NUM_ADRSEG) { // create new descriptor
    long base  =  read_reg(i.rs1())                 >> conf_Dline;
    long limit = (read_reg(i.rs2()) + conf_Dline-1) >> conf_Dline;
    dieif(base > limit, "base > limit");
    unsigned alloc = 1 << (conf_Dways-adrsegs);
    adrseg[adrsegs++] = adrseg_t(base, limit, alloc);
    adrseg[0].allowed &= ~alloc;
  }
  else
    die("too many adrsegs %d", adrsegs);
}

volatile long core_t::number_of_cores;

#define futex(a, b, c, d)  syscall(SYS_futex, a, b, c, d, 0, 0)


void update_time()
{
  long last_local = LONG_MAX;
  //char buffer[4096], *b=buffer;
  //b += sprintf(b, "cycles_run =");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    //b += sprintf(b, " %ld", p->cycles_run);
    if (p->now < last_local)
      last_local = p->now;
  }
  //b += sprintf(b, "\n");
  //fputs(buffer, stderr);
  if (last_local > system_clock && last_local < LONG_MAX) {
    //__atomic_store(&global_time, &last_local, __ATOMIC_RELAXED);
    system_clock = last_local;
    futex((int*)&system_clock, FUTEX_WAKE, INT_MAX, 0);
  }
}
void mem_t::sync_system_clock()
{
  static struct timespec timeout = { 0, 10000 };
  update_time();
  // wait until system_time catches up to our local_time
  for (long t=system_clock; t<now; t=system_clock) {
    //fprintf(stderr, "Me at %ld, %ld=system_clock\n", cycles_run, system_clock);
    futex((int*)&system_clock, FUTEX_WAIT, (int)t, &timeout);
  }
}

#undef futex

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
  saved_local_time = now;
  now = LONG_MAX;	// indicate we are stalled
  update_time();
  hart_t::proxy_syscall(sysnum);
  now = system_clock + SYSCALL_OVERHEAD;
  //now = system_clock;
  stall_time += now - t0; // accumulate stalled 
  update_time();
}


void start_time();
double elapse_time();

static void print_status()
{
  double realtime = elapse_time();
  long threads = 0;
  long tInsns = 0;
  long tCycles = 0;
  long tImisses = 0;
  long tDmisses = 0;
  long tMmisses = 0;
  for (core_t* p=core_t::list(); p; p=p->next()) {
    threads++;
    tInsns += p->executed();
    tCycles += p->cycles();
    tImisses += p->ic.misses();
    tDmisses += p->dc.misses();
    tMmisses += p->mc.misses();
  }
  fprintf(stderr, "\r\33[2K%12ld insns %4.2fM cycles/s in %3.1fs MIPS=%3.1f IPC=%4.2f I$=%4.2f%% D$=%4.2f%% M$=%4.2f%%",
	  tInsns, (double)tCycles/1e6/realtime, realtime, tInsns/1e6/realtime, (double)tInsns/tCycles,
  	  100.0*tImisses/tInsns, 100.0*tDmisses/tInsns, 100.0*tMmisses/tInsns);
#if 0
  char buf[4096];
  char* b = buf;
  b += sprintf(b, "\r\33[2K%12ld cycles %ld insns %3.1fs %3.1f MIPS IPC[I$,D$,M$](util)", total_cycles, total, realtime, total/1e6/realtime);
  char separator = '=';
  for (core_t* p=core_t::list(); p; p=p->next()) {
    b += sprintf(b, "%c%4.2f[%4.2f%%,%4.2f%%,%4.2f%%]", separator, (double)p->executed()/p->run_cycles(),
    		 100.0*p->ic.hits()/p->executed(), 100.0*p->dc.hits()/p->executed(), 100.0*p->mc.hits()/p->executed());
    if (p->cycles() == LONG_MAX)
      b += sprintf(b, "(***)");
    else
      b += sprintf(b, "(%4.2f%%)", 100.0*p->run_cycles()/p->run_time());
    separator = ',';
  }
  fputs(buf, stderr);
#endif
}

int status_function(void* arg)
{
  while (1) {
    sleep(1);
    print_status();
  }
}

void exitfunc()
{
  fprintf(stderr, "\n--------\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%ld] ", p->tid());
    p->mem()->print();
  }
  fprintf(stderr, "\n");
  print_status();
  fprintf(stderr, "\ncaveat exited normally");
}

inline void mem_t::insn_model(long pc)
{
  //  fprintf(stderr, "[%8lx] ", pc);
  //  labelpc(pc);
  //  disasm(pc);
  inc_count(pc);
  inc_cycle(pc);
  now += 1;
}

inline long mem_t::jump_model(long npc, long jpc)
{
  long pc = last_pc;
  long end = jpc + code.at(jpc).bytes();
  while (pc < end) {
    long ready = ic.lookup(pc, now+conf_Imiss, false);
    if (ready == now+conf_Imiss) {
      inc_imiss(pc);
      inc_cycle(pc, conf_Imiss);
      now += conf_Imiss;
    }
    pc = ic.linemask(pc) + ic.linesize();
  }
  last_pc = npc;
  return npc;
}
  
inline void mem_t::check_cache(long a, long pc, bool iswrite)
{
  long ready = mc.lookup(a, iswrite, now+conf_Dmiss);
  if (ready == now+conf_Dmiss) {
    inc_dmiss(pc);
    inc_cycle(pc, conf_Dmiss);
    now += conf_Dmiss;
  }
}

inline long mem_t::load_model(long a, long pc)
{
  check_cache(a, pc, false);
  dc.lookup(a, false, now+conf_Dmiss);
  return a;
}

inline long mem_t::store_model(long a, long pc)
{
  check_cache(a, pc, true);
  dc.lookup(a, true, now+conf_Dmiss);
  return a;
}

inline void mem_t::amo_model(long a, long pc)
{
  check_cache(a, pc, true);
  dc.lookup(a, true, now+conf_Dmiss);
}

mem_t::mem_t(long n)
  : perf_t(n),
    ic("Instruction", conf_Iways, conf_Iline, conf_Irows, false, true),
    dc("Data",        conf_Dways, conf_Dline, conf_Drows, true, false),
    mc("Multi",       conf_Dways, conf_Dline, conf_Drows, true,  true)
{
  now = 0;
}

void mem_t::print()
{
  ic.print();
  dc.print();
  mc.print();
}

core_t::core_t(long entry) : hart_t(mem()), mem_t(__sync_fetch_and_add(&number_of_cores, 1))
{
  init();
  last_pc = entry;
}

core_t::core_t(core_t* p) : hart_t(p, mem()), mem_t(__sync_fetch_and_add(&number_of_cores, 1))
{
  init();
  last_pc = p->last_pc;
}

void core_t::run_thread()
{
  fprintf(stderr, "Running [%ld]\n", tid());
  interpreter();
  die("should never get here");
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

  adrseg[0] = adrseg_t(LONG_MIN, LONG_MAX, ~0);
  adrsegs = 1;
  atexit(exitfunc);
  
#define STATUS_STACK_SIZE  (1<<16)
  char* stack = new char[STATUS_STACK_SIZE];
  stack += STATUS_STACK_SIZE;
  if (clone(status_function, stack, CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM, 0) == -1) {
    perror("clone failed");
    exit(-1);
  }

#ifdef DEBUG
    void dump_trace_handler(int nSIGnum);
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    sigemptyset(&action.sa_mask);
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = dump_trace_handler;
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGINT,  &action, NULL);
#endif

    mycpu->run_thread();
}

extern "C" {

#define POOL_SIZE  (1L<<31)	/* size of simulation memory pool */

static char* simpool;		/* base of memory pool */
static char* pooltop;		/* current allocation address */

void *malloc(size_t size)
{
  if (simpool == 0)
    simpool = pooltop = (char*)mmap((void*)0, POOL_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  size = (size + 15) & ~15;	// always allocate aligned objects
  if (pooltop+size > simpool+POOL_SIZE)
    return 0;
  return __sync_fetch_and_add(&pooltop, size);
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
