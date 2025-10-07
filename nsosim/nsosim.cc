
#include <limits.h>
#include <unistd.h>
#include <pthread.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"

option<long> conf_report("report", 1, "Status report per second");

void status_report()
{
  //  static long n = 1;
  //  fprintf(stderr, "\r%12ld", n++);
  //  return;
  
  double realtime = elapse_time();
  long total = 0;
  long flushed = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next()) {
    total += p->executed();
    flushed += p->flushed();
  }
  static double last_time;
  static long last_total;
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs(%ld) MIPS(%3.1f,%3.1f) ", total, realtime, flushed,
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


uintptr_t core_t::get_state()
{
  hart_t* h = this;
  memset(&s, 0, sizeof s);
  for (int i=0; i<32; i++)
    s.reg[i].x = h->s.xrf[i];
  for (int i=0; i<32; i++)
    s.reg[i+32].f = h->s.frf[i];
  s.fflags = h->s.fflags;
  s.frm = h->s.frm;
  return h->pc;
}

void core_t::put_state()
{
  hart_t* h = this;
  h->pc = pc;
  for (int i=0; i<32; i++)
    h->s.xrf[i] = s.reg[i].x;
  for (int i=0; i<32; i++)
    h->s.frf[i] = s.reg[i+32].f;
  h->s.fflags = s.fflags;
  h->s.frm = s.frm;
}

void dummy_simulator(hart_t* h, Header_t* bb, uintptr_t* ap)
{
}



int clone_proxy(class hart_t* h)
{
  core_t* parent = (core_t*)h;
  parent->put_state();
  core_t* child = new core_t(parent);
  return clone_thread(child);
}

void my_interpreter(hart_t* h)
{
  core_t* c = (core_t*)h;
  c->ooo_pipeline();
}

void core_t::ooo_pipeline()
{
  fprintf(stderr, "ooo_pipeline() starting\n");
  pc = get_state();
  Header_t* bb = find_bb(pc);
  i = insnp(bb+1);
  for (;;) {
    if (conf_show()) {
      put_state();
      print(pc, i, stdout);
    }
#if 0
    else if (conf_trace()) {
      fprintf(trace_file, "%lx\n", pc);
    }
#endif
    uintptr_t jumped = perform(i, pc);
    pc += i->compressed() ? 2 : 4;
    if (jumped || ++i >= insnp(bb+1)+bb->count) {
      if (jumped)
	pc = jumped;
      bb = find_bb(pc);
      i = insnp(bb+1);
    }
  }
}

void my_interpreter(core_t* c)
{
  c->ooo_pipeline();
}

void my_riscv_syscall(hart_t* h)
{
  core_t* c = (core_t*)h;
  long a0 = c->s.reg[10].x;
  long a1 = c->s.reg[11].x;
  long a2 = c->s.reg[12].x;
  long a3 = c->s.reg[13].x;
  long a4 = c->s.reg[14].x;
  long a5 = c->s.reg[15].x;
  long rvnum = c->s.reg[17].x;
  long rv = proxy_syscall(rvnum, a0, a1, a2, a3, a4, a5, c);
  c->s.reg[10].x = rv;
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
  
  core_t* cpu = new core_t(argc, argv, envp);
  cpu->simulator = dummy_simulator;
  cpu->clone = clone_proxy;
  cpu->riscv_syscall = my_riscv_syscall;
  cpu->interpreter = my_interpreter;
  atexit(exitfunc);

  if (conf_report() > 0) {
    pthread_t tnum;
    dieif(pthread_create(&tnum, 0, status_thread, 0), "failed to launch status_report thread");
  }
  
  start_time();
  my_interpreter(cpu);
  
  //  cpu->interpreter();
}
