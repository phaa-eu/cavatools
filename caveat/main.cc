/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

#include "options.h"
#include "caveat.h"

option<long> conf_report("report", 100000000, "Status report frequency");
option<bool> conf_quiet("quiet",	false, true,			"No status report");

class core_t : public hart_t {
  long executed;
  long next_report;
public:
  core_t(int argc, const char* argv[], const char* envp[]) :hart_t(argc, argv, envp) {
    executed=0; next_report=conf_report;
  }
  long more_insn(long n) { executed+=n; return executed; }
  static long total_count();
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }
  friend void simulator(hart_t* h, Header_t* bb);
  friend void status_report();
};

long core_t::total_count()
{
  long total = 0;
  for (core_t* p=core_t::list(); p; p=p->next())
    total += p->executed;
  return total;
}

static long customi = 0;

void status_report()
{
  if (conf_quiet)
    return;
  double realtime = elapse_time();
  long total = core_t::total_count();
  //  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  fprintf(stderr, "\r\33[2K%12ld(%ld) insns %3.1fs %3.1f MIPS ", total, customi, realtime, total/1e6/realtime);
  if (core_t::num_harts() <= 16) {
    char separator = '(';
    long total = core_t::total_count();
    for (core_t* p=core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      fprintf(stderr, "%1ld%%", 100*p->executed/total);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (core_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", core_t::num_harts());
}

void simulator(hart_t* h, Header_t* bb)
{
  core_t* c = (core_t*)h;
  for (Insn_t* i=insnp(bb+1); i<=insnp(bb)+bb->count; i++)
    if (attributes[i->opcode()] & ATTR_custom)
      customi++;
  if (c->more_insn(bb->count) > c->next_report) {
    status_report();
    c->next_report += conf_report;
  }
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
  
  
core_t* mycpu;

#ifdef DEBUG
void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
  //  ucontext_t* context = (ucontext_t*)vcontext;
  //  context->uc_mcontext.gregs[]
  fprintf(stderr, "\n\nsignal_handler(%d)\n", nSIGnum);
  //  strand_t* thisCPU = core_t::find(gettid())->
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
  mycpu = new core_t(argc, argv, envp);

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
  mycpu->interpreter(simulator);
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
