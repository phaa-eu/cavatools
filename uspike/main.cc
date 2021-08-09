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

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  fprintf(stderr, "gdb=%s, mhz=%d, isa=%s, vec=%s\n", gdb.val(), mhz.val(), isa.val(), vec.val());
  start_time(mhz.val());
  long entry = load_elf_binary(argv[0], 1);
  long sp = initialize_stack(argc, argv, envp, entry);
  void* mycpu = init_cpu(entry, sp, isa.val(), vec.val());
  if (gdb.val()) {
    OpenTcpLink(gdb.val());
    ProcessGdbCommand(mycpu);
    exit(0);
  }
  enum stop_reason reason;
  long insn_count = 0;
  do {
    reason = run_insns(stat.val()*1000000, insn_count);
    fprintf(stderr, "\r%ld instructions", insn_count);
  } while (reason == stop_normal);
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
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
