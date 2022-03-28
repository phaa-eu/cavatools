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
#include "core.h"

smp_t* smp;

using namespace std;
void* operator new(size_t size);
void operator delete(void*) noexcept;

option<long> conf_Jump("jump",	2,		"Taken branch pipeline flush cycles");

option<int> conf_Dmiss("dmiss",	10,		"Data cache miss penalty");
option<int> conf_Dline("dline",	6,		"Data cache log-base-2 line size");
option<int> conf_Drows("drows",	6,		"Data cache log-base-2 number of rows");

option<int> conf_cores("cores",	8,		"Maximum number of cores");
option<int> conf_busses("bus", 1,		"Number of busses");

option<>    conf_shm( "shm",	"caveat",	"Name of shared memory segment");
extern option<bool> conf_quiet;

int status_function(void* arg)
{
  while (1) {
    sleep(1);
    core_t::print_status();
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
  core_t::print_status();
  perf_t::close(shm_name);
  fprintf(stderr, "\ncaveat exited normally\n");
}

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
  long sp = initialize_stack(argc, argv, envp);
  core_t* mycpu = new core_t(code.entry());
  mycpu->write_reg(2, sp);	// x2 is stack pointer
  
  atexit(exitfunc);
  if (!conf_quiet) {
#define STATUS_STACK_SIZE  (1<<16)
    char* stack = new char[STATUS_STACK_SIZE];
    stack += STATUS_STACK_SIZE;
    if (clone(status_function, stack, CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM, 0) == -1) {
      perror("clone failed");
      exit(-1);
    }
  }

  smp = new smp_t(conf_Dline, conf_busses);
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
