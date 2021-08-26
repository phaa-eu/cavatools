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
  long sp = initialize_stack(argc, argv, envp, entry);
  cpu_t* mycpu = initial_cpu(entry, sp);

#ifdef DEBUG
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

  if (conf.gdb) {
    OpenTcpLink(conf.gdb);
    enum stop_reason reason;
    long insn_count = 0;
    do {
      ProcessGdbCommand(mycpu);
      do {
	//	reason = run_insns(stat*1000000, insn_count);
	reason = interpreter(mycpu, conf.stat*1000000);
	status_report();
      } while (reason == stop_normal);
      status_report();
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
    reason = interpreter(mycpu, conf.stat*1000000);
    status_report();
  } while (reason == stop_normal);
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
  // look for compare-and-swap pattern
  long possible=0, replaced=0;
  for (pc=lo; pc<hi; pc+=code.at(pc).compressed()?2:4) {
    Insn_t i = code.at(pc);
    if (!(i.opcode() == Op_lr_w || i.opcode() == Op_lr_d))
      continue;
    possible++;
    Insn_t i2 = code.at(pc+4);
    if (i2.opcode() == Op_ZERO) i2 = code.set(pc+4, decoder(code.image(pc+4), pc+4));
    if (!(i2.opcode() == Op_bne || i2.opcode() == Op_c_bnez)) continue;
    int len = 4 + (i2.opcode()==Op_c_bnez ? 2 : 4);
    Insn_t i3 = code.at(pc+len);
    if (i3.opcode() == Op_ZERO) i3 = code.set(pc+len, decoder(pc+len, code.image(pc+len)));
    if (!(i3.opcode() == Op_sc_w || i3.opcode() == Op_sc_d)) continue;
    // pattern found, check registers
    int addr_reg = i.rs1();
    int load_reg = i.rd();
    int test_reg = (i2.opcode() == Op_c_bnez) ? 0 : i2.rs2();
    int newv_reg = i3.rs2();
    int flag_reg = i3.rd();
    if (i2.rs1() != load_reg) continue;
    if (i3.rs1() != addr_reg) continue;
    // pattern is good
    Opcode_t op;
    if (len == 8) op = (i.opcode() == Op_lr_w) ? Op_cas_w   : Op_cas_d;
    else          op = (i.opcode() == Op_lr_w) ? Op_c_cas_w : Op_c_cas_d;
    code.set(pc, reg3insn(op, flag_reg, addr_reg, test_reg, newv_reg));
    replaced++;
  }
  fprintf(stderr, "%ld Load-Reserve found, %ld substitution failed\n", possible, possible-replaced);
}

#include "constants.h"

