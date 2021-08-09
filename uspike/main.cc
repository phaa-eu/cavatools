/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include "options.h"
#include "uspike.h"

insnSpace_t code;

static option<>      isa("isa",  "rv64imafdcv",			"RISC-V ISA string");
static option<>      vec("vec",  "vlen:128,elen:64,slen:128",	"Vector unit parameters");
static option<int>   mhz("mhz",  1000,				"Pretend MHz");
static option<long> stat("stat", 100,				"Status every M instructions");
static option<>      gdb("gdb",	 0, "localhost:1234", 		"Remote GDB on socket");

#ifdef DEBUG

Debug_t debug;

pctrace_t Debug_t::get()
{
  pctrace_t pt = trace[cursor];
  cursor = (cursor+1) % PCTRACEBUFSZ;
  return pt;
}

void Debug_t::insert(pctrace_t pt)
{
  trace[cursor] = pt;
  cursor = (cursor+1) % PCTRACEBUFSZ;
}

void Debug_t::insert(long c, long pc)
{
  trace[cursor].count = c;
  trace[cursor].pc    = pc;
  trace[cursor].val   = ~0l;
  trace[cursor].rn    = 0;
  cursor = (cursor+1) % PCTRACEBUFSZ;
}

void Debug_t::addval(int rn, long val)
{
  int c = (cursor+PCTRACEBUFSZ-1) % PCTRACEBUFSZ;
  trace[c].rn    = rn;
  trace[c].val   = val;
}

void Debug_t::print(FILE* f)
{
  for (int i=0; i<PCTRACEBUFSZ; i++) {
    pctrace_t t = get();
    if (t.rn != NOREG)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[t.rn], t.val);
    else
      fprintf(stderr, "%15ld %4s[%16s] ", t.count, "", "");
    disasm(t.pc);
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
  debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}

#endif

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  start_time(mhz.val());
  long entry = load_elf_binary(argv[0], 1);
  code.init(low_bound, high_bound);
  long sp = initialize_stack(argc, argv, envp, entry);
  void* mycpu = init_cpu(entry, sp, isa.val(), vec.val());
  if (gdb.val()) {
    OpenTcpLink(gdb.val());
    ProcessGdbCommand(mycpu);
    exit(0);
  }

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
  
  enum stop_reason reason;
  long insn_count = 0;
  do {
    reason = run_insns(stat.val()*1000000, insn_count);
    //reason = run_insns(1, insn_count);
    //reason = single_step(insn_count);
    fprintf(stderr, "\r%ld instructions", insn_count);
  } while (reason == stop_normal);
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  else if (reason == stop_exited)
    fprintf(stderr, "stop_exited\n");
  else
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

