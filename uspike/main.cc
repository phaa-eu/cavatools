/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#include "options.h"
#include "uspike.h"
#include "mmu.h"
#include "cpu.h"

void exit_func()
{
  fprintf(stderr, "\n");
  fprintf(stderr, "EXIT_FUNC() called\n\n");
  status_report();
  fprintf(stderr, "\n");
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
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  start_time();
  cpu_t* mycpu = new cpu_t(argc, argv, envp, new mmu_t());

  //#ifdef DEBUG
#if 0
  static struct sigaction action;
  memset(&action, 0, sizeof(struct sigaction));
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_sigaction = signal_handler;
  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGABRT, &action, NULL);
  sigaction(SIGINT,  &action, NULL);
  //  if (setjmp(return_to_top_level) != 0) {
  //    fprintf(stderr, "SIGSEGV signal was caught\n");
  //    debug.print();
  //    exit(-1);
  //  }
#endif

  dieif(atexit(exit_func), "atexit failed");
  long next_status_report = conf_stat*1000000L;
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
#if 1
      while (1) {
	long oldpc = mycpu->read_pc();
	if (!mycpu->single_step())
	  break;
	if (gdbNumContinue > conf_show)
	  show(mycpu, oldpc);
      }
#else
      while (mycpu->single_step())
	/* pass */ ;
#endif
      lastGdbSignal = SIGTRAP;
      ProcessGdbException();
    }
  }
  else
    interpreter(mycpu);
  return 0;
}

