/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

#include <map>

#include "options.h"
#include "caveat.h"

option<long> conf_report("report", 100000000, "Status report frequency");
option<bool> conf_quiet("quiet",	false, true,			"No status report");
option<> conf_gdb("gdb",	0, "localhost:1234", "Remote GDB connection");
option<bool> conf_show("show",	false, true,			"Show instruction trace");
option<bool> conf_calls("calls",	false, true,			"Show function calls and returns");

extern std::map<long, const char*> fname; // dictionary of pc->name

class hart_t : public hart_base_t {
  long executed;
  long next_report;
public:
  hart_t(int argc, const char* argv[], const char* envp[]) :hart_base_t(argc, argv, envp) {
    executed=0; next_report=conf_report;
  }
  long more_insn(long n) { executed+=n; return executed; }
  static long total_count();
  static hart_t* list() { return (hart_t*)hart_base_t::list(); }
  hart_t* next() { return (hart_t*)hart_base_t::next(); }
  friend void simulator(hart_base_t* h, Header_t* bb);
  friend void status_report();
};

long hart_t::total_count()
{
  long total = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next())
    total += p->executed;
  return total;
}

static long customi = 0;

void status_report()
{
  if (conf_quiet)
    return;
  double realtime = elapse_time();
  long total = hart_t::total_count();
  //  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  fprintf(stderr, "\r\33[2K%12ld(%ld) insns %3.1fs %3.1f MIPS ", total, customi, realtime, total/1e6/realtime);
  if (hart_t::num_harts() <= 16) {
    char separator = '(';
    long total = hart_t::total_count();
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      fprintf(stderr, "%1ld%%", 100*p->executed/total);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_t::num_harts());
}

void simulator(hart_base_t* h, Header_t* bb)
{
  hart_t* c = (hart_t*)h;
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
  mycpu = new hart_t(argc, argv, envp);
  start_time();

#ifdef DEBUG
  if (!conf_gdb) {
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
  }
#endif

  dieif(atexit(exit_func), "atexit failed");
  if (conf_gdb)
    controlled_by_gdb(conf_gdb, mycpu, simulator);
  else if (conf_show) {
    while (1)
      mycpu->single_step(simulator, true);
  }
  else if (conf_calls) {
    int indent = 0;
    while (1) {
#if 1
      long oldpc = mycpu->pc();
      Insn_t insn = decoder(oldpc);
      Opcode_t op = insn.opcode();
      
      if (op==Op_c_ret || op==Op_ret) {
	indent--;
      }
#endif 
      mycpu->single_step(simulator, true);
#if 1
      if (op==Op_jal || op==Op_c_jalr || op==Op_jalr) {
	indent++;
	long pc = mycpu->pc();
	auto it = fname.find(pc);
	//	fprintf(stderr, "\r%*s%s 0x%lx ", indent*4, "", op_name[op], pc);
	//	fprintf(stderr, " %s from 0x%lx\n", it==fname.end() ? "NOT FOUND" : it->second, oldpc);
	fprintf(stderr, "\r%*s%s\n", indent*4, "", it==fname.end() ? "NOT FOUND" : it->second);
      }
#endif
    }      
  }
  else
    mycpu->interpreter(simulator);
}
