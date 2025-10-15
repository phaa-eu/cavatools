#include <cassert>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"

option<long> conf_report("report", 1, "Status report per second");

#if 1
option<int> conf_fp("fp", 3, "Latency floating point");
option<int> conf_ld("ld", 4, "Latency loads");
option<int> conf_st("st", 20, "Latency stores");
option<int> conf_alu("alu", 1, "Latency ALU");
#else
option<int> conf_fp("fp", 2, "Latency floating point");
option<int> conf_ld("ld", 2, "Latency loads");
option<int> conf_st("st", 2, "Latency stores");
option<int> conf_alu("alu", 2, "Latency ALU");
#endif

uint8_t latency[Number_of_Opcodes];


void status_report()
{
  double realtime = elapse_time();
  long unsigned total_cycles = 0;
  for (core_t* p=core_t::list(); p; p=p->next()) {
    total_cycles += p->cpu_cycles();
  }
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs CPS=%3.1f IPC=", total_cycles, realtime, total_cycles/1e6/realtime);

  if (hart_t::num_harts() <= 16) {
    char separator = '(';
    for (core_t* p=core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      fprintf(stderr, "%5.3f", (double)p->executed()/p->cpu_cycles());
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_t::num_harts());
}

void* status_thread(void* arg)
{
  while (1) {
    usleep(1000000/conf_report());
    status_report();
  }
}

void exitfunc()
{
  fprintf(stderr, "\nNormal exit\n");
  status_report();
  fprintf(stderr, "\n");
}



int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "nsosim: RISC-V non-speculative out-of-order simulator");
  if (argc == 0)
    help_exit();
#if 0
  if (conf_trace())
    trace_file = fopen(conf_trace(), "w");
#endif

  for (int k=0; k<Number_of_Opcodes; ++k) {
    Opcode_t op = (Opcode_t)k;
    ATTR_bv_t a = attributes[op];
    if      (a & ATTR_fp) latency[op] = conf_fp();
    else if (a & ATTR_ld) latency[op] = conf_ld();
    else if (a & ATTR_st) latency[op] = conf_st();
    else                  latency[op] = conf_alu();
  }
  
  core_t* cpu = new core_t(argc, argv, envp);
  cpu->clone = clone_proxy;
  cpu->riscv_syscall = ooo_riscv_syscall;
  atexit(exitfunc);

  if (0 && conf_report() > 0 && !conf_show()) {
    pthread_t tnum;
    dieif(pthread_create(&tnum, 0, status_thread, 0), "failed to launch status_report thread");
  }
  
  start_time();
  cpu->init_simulator();
  cpu->interactive();
}
