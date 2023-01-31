/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

#include "options.h"
#include "caveat.h"
#include "instructions.h"
#include "strand.h"

option<bool> conf_quiet("quiet",	false, true,			"No status report");

void my_status(class hart_t* p)
{
  fprintf(stderr, "%1ld%%", 100*p->executed()/hart_t::total_count());
}

void status_report(statfunc_t my_status)
{
  if (conf_quiet)
    return;
  double realtime = elapse_time();
  long total = hart_t::total_count();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  if (hart_t::threads() <= 16) {
    char separator = '(';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      //      fprintf(stderr, "%1ld%%", 100*p->executed()/total);
      my_status(p);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::threads() > 1)
    fprintf(stderr, "(%d cores)", hart_t::threads());
}

void exit_func()
{
  fprintf(stderr, "\n");
  fprintf(stderr, "EXIT_FUNC() called\n\n");
  status_report(my_status);
  fprintf(stderr, "\n");
}  

static jmp_buf return_to_top_level;

static void segv_handler(int, siginfo_t*, void*) {
  longjmp(return_to_top_level, 1);
}

void nop_simulator(class hart_t* p, long pc, Insn_t* begin, long count, long* addresses)
{
}
  
  
hart_t* mycpu;

#ifdef DEBUG
void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
  //  ucontext_t* context = (ucontext_t*)vcontext;
  //  context->uc_mcontext.gregs[]
  fprintf(stderr, "\n\nsignal_handler(%d)\n", nSIGnum);
  //  strand_t* thisCPU = hart_t::find(gettid())->
  //  thisCPU->debug.print();
  mycpu->strand->debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}
#endif

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  start_time();
  mycpu = new hart_t(argc, argv, envp);

#ifdef DEBUG
  static struct sigaction action;
  memset(&action, 0, sizeof(struct sigaction));
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_sigaction = segv_handler;
  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGABRT, &action, NULL);
  sigaction(SIGINT,  &action, NULL);
  if (setjmp(return_to_top_level) != 0) {
    fprintf(stderr, "SIGSEGV signal was caught\n");
    mycpu->print_debug_trace();
    exit(-1);
  }
#endif

  mycpu->interpreter(nop_simulator, my_status);
}

#if 0
  dieif(atexit(exit_func), "atexit failed");
  //  enum stop_reason reason;
  if (conf_gdb) {
    gdb_pc = mycpu->ptr_pc();
    gdb_reg = mycpu->reg_file();
    OpenTcpLink(conf_gdb);
    signal(SIGABRT, signal_handler);
    signal(SIGFPE,  signal_handler);
    signal(SIGSEGV, signal_handler);
    //    signal(SIGILL,  signal_handler);
    //    signal(SIGINT,  signal_handler);
    //    signal(SIGTERM, signal_handler);
    while (1) {
      if (setjmp(mainGdbJmpBuf))
	ProcessGdbException();
      ProcessGdbCommand();
      while (1) {
	long oldpc = mycpu->read_pc();
	//	if (!mycpu->interpreter(1))
	//	  break;
	//	if (gdbNumContinue > conf_show)
	//	  show(mycpu, oldpc);
      }
      mycpu->single_step();
      lastGdbSignal = SIGTRAP;
      ProcessGdbException();
    }
  }
  else {
    while (1) {
      mycpu->interpreter();
      status_report();
    }
  }
  return 0;
}
#endif


#if 0

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

#endif
