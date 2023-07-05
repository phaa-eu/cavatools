/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>

//#include <map>

#include "options.h"
#include "caveat.h"

option<long> conf_report("report", 1, "Status report per second");
option<>     conf_gdb("gdb",	0, "localhost:1234", "Remote GDB connection");
option<bool> conf_calls("calls",	false, true,			"Show function calls and returns");

option<long> conf_tcache("tcache", 64*1024, "Binary translation cache size");
option<long> conf_hash("hash", 20011, "Hash table size, best if prime number");

extern option<bool> conf_show;

//extern std::map<long, const char*> fname; // dictionary of pc->name

class hart_t : public hart_base_t {
  long _executed;
public:
  hart_t(int argc, const char* argv[], const char* envp[])
    :hart_base_t(argc, argv, envp) { _executed=0; }
  hart_t(hart_base_t* from) :hart_base_t(from) { _executed=0; }

  long executed() { return _executed; }
  void more_insn(long n) { _executed+=n; }
  static long total_count();
  static hart_t* list() { return (hart_t*)hart_base_t::list(); }
  hart_t* next() { return (hart_t*)hart_base_t::next(); }
  static hart_t* find(int tid) { return (hart_t*)hart_base_t::find(tid); }
  friend void simulator(hart_base_t* h, Header_t* bb, uint64_t* counters);
  friend void status_report();
};

long hart_t::total_count()
{
  long total = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next())
    total += p->executed();
  return total;
}

void status_report()
{
  //  static long n = 1;
  //  fprintf(stderr, "\r%12ld", n++);
  //  return;
  
  double realtime = elapse_time();
  long total = hart_t::total_count();
  static double last_time;
  static long last_total;
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs MIPS(%3.1f,%3.1f) ", total, realtime,
	  (total-last_total)/1e6/(realtime-last_time), total/1e6/realtime);
  last_time = realtime;
  last_total = total;
  if (hart_t::num_harts() <= 16 && total > 0) {
    char separator = '(';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      fprintf(stderr, "%1ld%%", 100*p->executed()/total);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_t::num_harts());
}

void simulator(hart_base_t* h, long index)
{
  hart_t* c = (hart_t*)h;
  const Header_t* bb = tcache.bbptr(index);
  c->more_insn(bb->count);
}

long clone_proxy(class hart_base_t* h, long* args)
{
  hart_t* child = new hart_t(h);
  return clone_thread(child);
}

long syscall_proxy(class hart_base_t* h, long num, long* args)
{
  return host_syscall(num, args);
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
  fprintf(stderr, "\n\nsignal_handler, signum=%d, tid=%d\n", nSIGnum, gettid());
  hart_t* thisCPU = hart_t::find(gettid());
  thisCPU->debug_print();
  //  mycpu->debug_print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}
#endif

void* status_thread(void* arg)
{
  while (1) {
    usleep(1000000/conf_report());
    status_report();
  }
}

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  // create translation cache
  tcache.initialize(conf_tcache(), conf_hash());
  // before creating harts
  mycpu = new hart_t(argc, argv, envp);
  mycpu->simulator = simulator;
  mycpu->clone = clone_proxy;
  mycpu->syscall = syscall_proxy;
  start_time();

#ifdef DEBUG
  if (!conf_gdb()) {
    static struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    sigemptyset(&action.sa_mask);
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    //    action.sa_sigaction = segv_handler;
    action.sa_sigaction = signal_handler;
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

  if (conf_gdb())
    controlled_by_gdb(conf_gdb(), mycpu);
  else if (conf_show()) {
    while (1)
      mycpu->single_step(true);
  }
  else if (conf_calls()) {
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
      mycpu->single_step(true);
#if 1
      if (op==Op_jal || op==Op_c_jalr || op==Op_jalr) {
	indent++;
	//	long pc = mycpu->pc();
	//	auto it = fname.find(pc);
	//	fprintf(stderr, "\r%*s%s 0x%lx ", indent*4, "", op_name[op], pc);
	//	fprintf(stderr, " %s from 0x%lx\n", it==fname.end() ? "NOT FOUND" : it->second, oldpc);
	//	fprintf(stderr, "\r%*s%s\n", indent*4, "", it==fname.end() ? "NOT FOUND" : it->second);
	
	//fprintf(stderr, "\r%*s%s\n", indent*4, "", func_name(mycpu->pc()));
      }
#endif
    }      
  }
  else {
    if (conf_report() > 0) {
      pthread_t tnum;
      dieif(pthread_create(&tnum, 0, status_thread, 0), "failed to launch status_report thread");
    }
    dbmsg("Begin executing at 0x%lx", mycpu->pc());
    int retval = mycpu->interpreter();
    //terminate_threads();
    fprintf(stderr, "\n");
    status_report();
    fprintf(stderr, "\n\n");
    return retval;
  }
}
