/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include "options.h"
#include "uspike.h"

configuration_t conf;
insnSpace_t code;

#ifdef DEBUG

Debug_t debug;

pctrace_t Debug_t::get()
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  return trace[cursor];
}

void Debug_t::insert(pctrace_t pt)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor] = pt;
}

void Debug_t::insert(long c, long pc)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor].count = c;
  trace[cursor].pc    = pc;
  trace[cursor].val   = ~0l;
  trace[cursor].rn    = GPREG;
}

void Debug_t::addval(int rn, long val)
{
  trace[cursor].rn    = rn;
  trace[cursor].val   = val;
}

void Debug_t::print(FILE* f)
{
  for (int i=0; i<PCTRACEBUFSZ; i++) {
    pctrace_t t = get();
    if (t.rn != NOREG)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[t.rn], t.val);
    else
      fprintf(stderr, "%15ld %4s[%16s] ", t.count, "", "");
    labelpc(t.pc);
    if (code.valid(t.pc))
      disasm(t.pc, "");
    fprintf(stderr, "\n");
  }
}

#include <signal.h>
//#include <setjmp.h>

//jmp_buf return_to_top_level;

void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
  //  ucontext_t* context = (ucontext_t*)vcontext;
  //  context->uc_mcontext.gregs[]
  fprintf(stderr, "\n\nsignal_handler(%d)\n", nSIGnum);
  if (conf.gdb) {
    HandleException(nSIGnum);
    ProcessGdbCommand();
  }
  else
    debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}

#endif

int main(int argc, const char* argv[], const char* envp[])
{
  new option<>     (conf.isa,	"isa",		"rv64imafdcv",			"RISC-V ISA string");
  new option<>     (conf.vec,	"vec",		"vlen:128,elen:64,slen:128",	"Vector unit parameters");
  new option<int>  (conf.mhz,	"mhz",		1000,				"Pretend MHz");
  new option<int>  (conf.stat,	"stat",	100,				"Status every M instructions");
  new option<bool> (conf.show,	"show",	false, true,			"Show instructions executing");
  new option<>     (conf.gdb,	"gdb", 	0, "localhost:1234", 		"Remote GDB on socket");
  new option<int>  (conf.ecall,	"ecall",	0, 1,				"Report every N ecalls");
  
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  start_time(conf.mhz);
  long entry = load_elf_binary(argv[0], 1);
  code.init(low_bound, high_bound);
  long sp = initialize_stack(argc, argv, envp, entry);
  void* mycpu = init_cpu(entry, sp);

#ifdef DEBUG
  static struct sigaction action;
  memset(&action, 0, sizeof(struct sigaction));
  sigemptyset(&action.sa_mask);
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_sigaction = signal_handler;
  sigaction(SIGSEGV, &action, NULL);
  //  if (setjmp(return_to_top_level) != 0) {
  //    fprintf(stderr, "SIGSEGV signal was caught\n");
  //    debug.print();
  //    exit(-1);
  //  }
#endif

  if (conf.gdb) {
    OpenTcpLink(conf.gdb);
    enum stop_reason reason;
    long insn_count = 0;
    do {
      ProcessGdbCommand(mycpu);
      do {
	//	reason = run_insns(stat*1000000, insn_count);
	reason = interpreter(mycpu, conf.stat*1000000, insn_count);
	status_report(insn_count);
      } while (reason == stop_normal);
      status_report(insn_count);
      fprintf(stderr, "\n");
      if (reason == stop_breakpoint)
	HandleException(SIGTRAP);
      else if (reason != stop_exited)
	die("unknown reason %d", reason);
    } while (reason != stop_exited);
    exit(0);
  }
  
  enum stop_reason reason;
  long insn_count = 0;
  do {
    reason = interpreter(mycpu, conf.stat*1000000, insn_count);
    status_report(insn_count);
  } while (reason == stop_normal);
  fprintf(stderr, "\n");
  status_report(insn_count);
  fprintf(stderr, "\n");
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  else if (reason != stop_exited)
    die("unknown reason %d", reason);
  return 0;
}

void insnSpace_t::init(long lo, long hi)
{
  base=lo;
  limit=hi;
  int n = (hi-lo)/2;
  predecoded=new Insn_t[n];
  memset(predecoded, 0, n*sizeof(Insn_t));
}

#include "constants.h"

