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

option<long> conf_report("report", 100000000, "Status report frequency");
option<bool> conf_quiet("quiet",	false, true,			"No status report");

void status_report()
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
      fprintf(stderr, "%1ld%%", 100*p->executed()/hart_t::total_count());
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
  status_report();
  fprintf(stderr, "\n");
}  

static jmp_buf return_to_top_level;

static void segv_handler(int, siginfo_t*, void*) {
  longjmp(return_to_top_level, 1);
}

void hart_t::simulator(long pc, Insn_t* begin, long count, long* addresses)
{
  if ((_executed+=count) >= next_report) {
    status_report();
    next_report += conf_report;
  }
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
  mycpu->debug_print();
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

  //  mycpu->interpreter(nop_simulator, my_status);
  mycpu->interpreter();
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
