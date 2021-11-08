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

//#define NOCACHE

using namespace std;
void* operator new(size_t size);
void operator delete(void*) noexcept;

option<long> conf_Jump("jump",	2,		"Taken branch pipeline flush cycles");

option<int> conf_Imiss("imiss",	10,		"Instruction cache miss penalty");
option<int> conf_Iline("iline",	6,		"Instruction cache log-base-2 line size");
option<int> conf_Irows("irows",	6,		"Instruction cache log-base-2 number of rows");

option<int> conf_Dmiss("dmiss",	10,		"Data cache miss penalty");
option<int> conf_Dline("dline",	6,		"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	6,		"Data cache log-base-2 number of rows");

option<int> conf_cores("cores",	8,		"Maximum number of cores");

option<>    conf_shm( "shm",	"caveat",	"Name of shared memory segment");
option<long> conf_report("report", 10,		"Status report every N million instructions");

volatile long system_clock;

class core_t : public hart_t {
  perf_t perf;
  volatile long now;
  long last_pc;
  long start_time;
  long stall_time;
  long saved_local_time;
  static volatile long number_of_cores;
  core_t();
public:
  fsm_cache<4, true,  true>	dc;
  core_t(long entry);
  hart_t* newcore() { core_t* h=new core_t(); h->copy_state(this); h->last_pc=read_pc(); return h; }
  void proxy_syscall(long sysnum);
  int run_thread();
  void sync_system_clock();
  friend void update_time();
  friend void print_status();

  void check_cache(long a, long pc, bool iswrite)
  {
    long delay = iswrite ? dc.write(a) : dc.read(a);
    if (delay >= 0) {
      perf.inc_dmiss(pc);
      perf.inc_cycle(pc, conf_Dmiss);
      now += conf_Dmiss;
    }
  }
  long load_model( long a,  long pc) { check_cache(a, pc, false); return a; }
  long store_model(long a,  long pc) { check_cache(a, pc, true ); return a; }
  void amo_model(  long a,  long pc) { check_cache(a, pc, true );           }
  void custom(long pc) { }
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  int number() { return perf.number(); }
  bool stalled() { return now == LONG_MAX; }
  long cycles() { return stalled() ? saved_local_time : now; }
  long stalls() { return stall_time; }
  long run_time() { return cycles() - start_time; }
  long run_cycles() { return run_time() - stall_time; }
};

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
void core_t::sync_system_clock()
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
  long pc = read_pc();
  long saved_count = perf.count(pc);
  perf.inc_count(pc, -saved_count-1);
  update_time();
  sync_system_clock();
  long t0 = system_clock;
  saved_local_time = now;
  now = LONG_MAX;	// indicate we are stalled
  update_time();
  hart_t::proxy_syscall(sysnum);
  now = system_clock + SYSCALL_OVERHEAD;
  stall_time += now - t0; // accumulate stalled 
  update_time();
  perf.inc_count(pc, saved_count+1);
}


void start_time();
double elapse_time();

void print_status()
{
  double realtime = elapse_time();
  long threads = 0;
  long tInsns = 0;
  double aUtil = 0.0;
  for (core_t* p=core_t::list(); p; p=p->next()) {
    threads++;
    tInsns += p->executed();
    aUtil += (double)p->run_cycles()/p->run_time();
  }
  aUtil /= threads;
  update_time();
  long now = system_clock;
  char buf[4096];
  char* b = buf;
  //  b += sprintf(b, "\r\33[2K%12gB insns %3.1fM cycles/s in %3.1fs MIPS=%3.1f(%3.1f%%) IPC(mpki,u)",
  //	       tInsns/1e9, (double)now/1e6/realtime, realtime, tInsns/1e6/realtime, 100.0*aUtil);
  b += sprintf(b, "\r\33[2K%12ld insns %3.1fs MCPS=%3.1f MIPS=%3.1f(%3.1f%%) IPC(mpki,u)",
	       tInsns, realtime, (double)now/1e6/realtime, tInsns/1e6/realtime, 100.0*aUtil);
  char separator = '=';
  for (core_t* p=core_t::list(); p; p=p->next()) {
    double ipc = (double)p->executed()/p->run_cycles();
    long dmpk = 1000.0*p->dc.misses()/p->executed() + 0.5;
    long util = 100.0*p->run_cycles()/p->run_time() + 0.5;
    b += sprintf(b, "%c%4.2f(%ld,%ld%%)", separator, ipc, dmpk, util);
    separator = ',';
  }
  fputs(buf, stderr);
}

int status_function(void* arg)
{
  while (1) {
    sleep(1);
    print_status();
  }
}

static char shm_name[1024];

void exitfunc()
{
  core_t* p = core_t::list();
  // assume all D caches are same
  long size = p->dc.linesize() * p->dc.rows() * p->dc.ways();
  fprintf(stderr, "\n--------\nL1 D-cache size ");
  if      (size >= 1024*1024)  fprintf(stderr, "%3.1f MB\n", size/1024.0/1024);
  else if (size >=      1024)  fprintf(stderr, "%3.1f KB\n", size/1024.0);
  else                         fprintf(stderr, "%ld B\n", size);
  long tInsns = 0;
  for (p=core_t::list(); p; p=p->next())
    tInsns += p->executed();
  fprintf(stderr, "Core  References Writes  Miss  MPKI UPKI\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "%4d %10ld  %5.2f%% %5.2f%% %4ld %4ld\n",
	    p->number(), p->dc.refs(), 100.0*p->dc.updates()/p->dc.refs(), 100.0*p->dc.misses()/p->dc.refs(),
	    (long)(1000.0*p->dc.misses()/tInsns+0.5), (long)(1000.0*p->dc.evictions()/tInsns+0.5));
  }
  fprintf(stderr, "\n");
  print_status();
  perf_t::close(shm_name);
  fprintf(stderr, "\ncaveat exited normally\n");
}

core_t::core_t() : perf(__sync_fetch_and_add(&number_of_cores, 1)),
		   dc(conf_Dline, conf_Drows, "Data")
{
  now = start_time = system_clock;
  stall_time = 0;
}

core_t::core_t(long entry) : core_t()
{
  last_pc = entry;
}

int core_t::run_thread()
{
  try {
    while (1) {
      volatile count_t* c = perf.count_ptr(read_pc());
      long end;
      now += interpreter(end);
      for (; c<perf.count_ptr(end); c++) {
	c->executed++;
	c->cycles++;
      }
    }
  } catch (int return_code) {
    //    fprintf(stderr, "trap_exit_system_call!\n");
    return return_code;
  }
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

  if (std::getenv("MPI_LOCALNRANKS"))
    sprintf(shm_name, "%s.%d", conf_shm(), getpid());
  else
    sprintf(shm_name, "%s", conf_shm());
  perf_t::create(code.base(), code.limit(), conf_cores, shm_name);
  //  for (int i=0; i<perf_t::cores(); i++)
  //    new perf_t(i);
  long sp = initialize_stack(argc, argv, envp);
  core_t* mycpu = new core_t(code.entry());
  mycpu->write_reg(2, sp);	// x2 is stack pointer
  
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
