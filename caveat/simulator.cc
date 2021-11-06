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

option<>    conf_perf( "perf",	"caveat",	"Name of shared memory segment");
option<long> conf_report("report", 10,		"Status report every N million instructions");

volatile long system_clock;

class core_t : public hart_t, perf_t {
  long last_pc;
  long start_time;
  long stall_time;
  long saved_local_time;
  static volatile long number_of_cores;
public:
  volatile long now;
  fsm_cache<4, true,  true>	dc;

  core_t();
  
  hart_t* duplicate();
  void proxy_syscall(long sysnum);
  void run_thread();

  void simulate(event_t* buffer, int last, int now);
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  bool stalled() { return now == LONG_MAX; }
  long cycles() { return stalled() ? saved_local_time : now; }
  long stalls() { return stall_time; }
  long run_time() { return cycles() - start_time; }
  long run_cycles() { return run_time() - stall_time; }

  void sync_system_clock();
  void print();
};

volatile long core_t::number_of_cores;



 
void core_t::simulate(event_t* buffer, int c, int last)
{
  long pc = last_pc;
  c = (c+1) % EVENT_BUFFER_SIZE;
  while (c != last) {
    //fprintf(stderr, "type=%d delta=%d, pc=%lx\n", buffer[c].type, buffer[c].delta, pc);
    dieif(buffer[c].type == jump_event, "not expecting jump_event");
    long endpc = last_pc + 2*buffer[c].delta;
    while (pc < endpc) {
      inc_cycle(pc);
      now++;
      pc += code.at(pc).bytes();
    }
    long addr = buffer[c].address;
    long delay = (buffer[c].type==load_event) ? dc.read(addr) : dc.write(addr);
    if (delay >= 0) {
      inc_dmiss(pc);
      inc_cycle(pc, conf_Dmiss());
      now += conf_Dmiss();
    }
    last_pc = pc;
    c = (c+1) % EVENT_BUFFER_SIZE;
  }
  //fprintf(stderr, "type=%d delta=%d, pc=%lx\n", buffer[c].type, buffer[c].delta, pc);
  dieif(buffer[c].type != jump_event, "expecting jump_event");
  long endpc = last_pc + 2*buffer[c].delta;
  while (pc < endpc) {
    inc_cycle(pc);
    now++;
    pc += code.at(pc).bytes();
  }
  inc_cycle(pc, conf_Jump());
  now += conf_Jump();
  last_pc = buffer[c].address;
}

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
  long saved_count = count(pc);
  inc_count(pc, -saved_count-1);
  update_time();
  sync_system_clock();
  long t0 = system_clock;
  saved_local_time = now;
  now = LONG_MAX;	// indicate we are stalled
  update_time();

  switch (sysnum) {
    //case SYS_sched_yield:
  case 24:
    write_reg(10, 0);
    break;
  default:
    hart_t::proxy_syscall(sysnum);
  }
  
  now = system_clock + SYSCALL_OVERHEAD;
  //now = system_clock;
  stall_time += now - t0; // accumulate stalled 
  update_time();

  inc_count(pc, saved_count+1);
}


void start_time();
double elapse_time();

static void print_status()
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
  b += sprintf(b, "\r\33[2K[%6d] %gB insns %3.1fs for %3.1fM cycles/s util=%3.1f%% IPC[util,D$mpk]",
	       getpid(), tInsns/1e9, realtime, (double)now/1e6/realtime, 100.0*aUtil);
  char separator = '=';
  for (core_t* p=core_t::list(); p; p=p->next()) {
    double ipc = (double)p->executed()/p->run_cycles();
    double util = 100.0*p->run_cycles()/p->run_time();
    double dmpk = 1000.0*p->dc.misses/p->executed();
    b += sprintf(b, "%c%4.2f[%3.1f%%,%3.1f]", separator, ipc, util, dmpk);
    separator = ',';
  }
  fputs(buf, stderr);
}

int status_function(void* arg)
{
  while (1) {
    long msec = rand() % 1024;
    usleep(1000*msec);
    print_status();
  }
}

void exitfunc()
{
  fprintf(stderr, "\n--------\n");
  for (core_t* p=core_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%ld] ", p->tid());
    p->print();
  }
  fprintf(stderr, "\n");
  print_status();
  fprintf(stderr, "\ncaveat exited normally");
}

void core_t::print()
{
  dc.print();
}
  
core_t::core_t() : hart_t(),
		   perf_t(__sync_fetch_and_add(&number_of_cores, 1)),
		   dc(conf_Dline(), conf_Drows(), "Data")
{
  now = start_time = system_clock;
  stall_time = 0;
  last_event = last_pc = read_pc();
}

hart_t* core_t::duplicate()
{
  core_t* h = new core_t();
  h->copy_state(this);
  last_event = last_pc = read_pc();
  return h;
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

  char buf[1024];
  sprintf(buf, "%s.%d", conf_perf(), getpid());
  perf_t::create(code.base(), code.limit(), conf_cores(), buf);
  for (int i=0; i<perf_t::cores(); i++)
    new perf_t(i);
  long sp = initialize_stack(argc, argv, envp);
  core_t* mycpu = new core_t();
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
