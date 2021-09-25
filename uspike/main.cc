/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"

option<long> conf_report("report",	100,				"Status report every N million instructions");
option<long> conf_show("show",		0, 				"Trace execution after N gdb continue");
option<>     conf_gdb("gdb",		0, "localhost:1234", 		"Remote GDB on socket");

void exit_func()
{
  fprintf(stderr, "\n");
  hart_t::status_report();
  fprintf(stderr, "\nuspike terminated normally\n");
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

#ifdef DEBUG
void dump_trace_handler(int nSIGnum)
{
  fprintf(stderr, "Killed by signal %d\n", nSIGnum);
  hart_t* mycpu = hart_t::find(gettid());
  mycpu->debug.print();
  exit(-1);
}
#endif

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  start_time();
  code.loadelf(argv[0]);
  long sp = initialize_stack(argc, argv, envp);
  hart_t* mycpu = new hart_t(new mmu_t());
  mycpu->write_reg(2, sp);	// x2 is stack pointer

  dieif(atexit(exit_func), "atexit failed");
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
	if (!mycpu->interpreter(1))
	  break;
	if (gdbNumContinue > conf_show)
	  show(mycpu, oldpc);
      }
      lastGdbSignal = SIGTRAP;
      ProcessGdbException();
    }
  }
  else {
#ifdef DEBUG
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
  return 0;
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
