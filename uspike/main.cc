/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <signal.h>

#include "options.h"
#include "cpu.h"
#include "uspike.h"

configuration_t conf;
insnSpace_t code;

void* operator new(size_t size)
{
  //  fprintf(stderr, "operator new(%ld)\n", size);
  extern void* malloc(size_t);
  return malloc(size);
}
void operator delete(void*) noexcept
{
}


//#include <setjmp.h>

//jmp_buf return_to_top_level;

void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext);

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
  long sp = initialize_stack(argc, argv, envp);
  cpu_t* mycpu = initial_cpu(entry, sp);

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

  long next_status_report = conf.stat*1000000L;
  enum stop_reason reason;
  if (conf.gdb)
    OpenTcpLink(conf.gdb);
  do {
    if (conf.gdb)
      ProcessGdbCommand(mycpu);
    do {
      reason = interpreter(mycpu, 10000);
      if (cpu_t::total_count() > next_status_report) {
	status_report();
	next_status_report += conf.stat*1000000L;
      }
    } while (reason == stop_normal);
    status_report();
    fprintf(stderr, "\n");
    if (reason == stop_breakpoint)
      HandleException(SIGTRAP);
    else if (reason != stop_exited)
      die("unknown reason %d", reason);
  } while (reason != stop_exited);
  fprintf(stderr, "\n");
  status_report();
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
  // Predecode instruction code segment
  long pc = lo;
  while (pc < hi) {
    Insn_t i = code.set(pc, decoder(code.image(pc), pc));
    pc += i.compressed() ? 2 : 4;
  }
  substitute_cas(lo, hi);
}

#include "constants.h"

