/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>

//#include <gperftools/profiler.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"

option<long> conf_report("report",	100,				"Status report every N million instructions");
option<>     conf_gdb("gdb",		0, "localhost:1234", 		"Remote GDB on socket");

static void print_status()
{
  char buf[4096];
  char* b = buf;
  double realtime = elapse_time();
  long threads = 0;
  long total = 0;  
  for (hart_t* p=hart_t::list(); p; p=p->next()) {
    threads++;
    total += p->executed();
  }
  b += sprintf(b, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  if (threads <= 16) {
    char separator = '(';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      b += sprintf(b, "%c%1ld%%", separator, 100*p->executed()/total);
      separator = ',';
    }
    b += sprintf(b, ")");
  }
  else if (threads > 1)
    b += sprintf(b, "(%ld cores)", threads);
  fputs(buf, stderr);
}

int status_function(void* arg)
{
  while (1) {
    sleep(1);
    print_status();
  }
  return 0;
}

void exit_func()
{
  fprintf(stderr, "\n");
  print_status();
  fprintf(stderr, "\nuspike terminated normally\n");
  //ProfilerStop();
}  

extern "C" {
  extern int lastGdbSignal;
  extern jmp_buf mainGdbJmpBuf;
  void signal_handler(int nSIGnum);
  void ProcessGdbCommand();
  void ProcessGdbException();
  void OpenTcpLink(const char* name);
  extern long *gdb_pc;
  extern long *gdb_reg;
  extern long gdbNumContinue;
};

int main(int argc, const char* argv[], const char* envp[])
{
  //ProfilerStart("uspike.prof");
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  start_time();
  code.loadelf(argv[0]);
  long sp = initialize_stack(argc, argv, envp);
  hart_t* mycpu = new hart_t();
  mycpu->write_reg(2, sp);	// x2 is stack pointer

  dieif(atexit(exit_func), "atexit failed");
  int rc = 0;
  if (conf_gdb) {
    gdb_pc = mycpu->ptr_pc();
    gdb_reg = mycpu->reg_file();
    OpenTcpLink(conf_gdb);
    signal(SIGABRT, signal_handler);
    signal(SIGFPE,  signal_handler);
    signal(SIGSEGV, signal_handler);
    while (1) {
      if (setjmp(mainGdbJmpBuf))
	ProcessGdbException();
      ProcessGdbCommand();
      while (1) {
	long oldpc = mycpu->read_pc();
	if (!mycpu->single_step())
	  break;
      }
      lastGdbSignal = SIGTRAP;
      ProcessGdbException();
    }
  }
  else {
    // spawn status report thread
    extern option<bool> conf_quiet;
    if (!conf_quiet) {
#define STATUS_STACK_SIZE  (1<<16)
      char* stack = new char[STATUS_STACK_SIZE];
      stack += STATUS_STACK_SIZE;
      if (clone(status_function, stack, CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM, 0) == -1) {
	perror("clone failed");
	exit(-1);
      }
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
    rc = mycpu->run_thread();
  }
  return rc;
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
