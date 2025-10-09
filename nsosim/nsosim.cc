#include <cassert>
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
    h->s.xrf[i] = s.reg[regmap[i]].x;
  for (int i=0; i<32; i++)
    h->s.frf[i] = s.reg[regmap[i+32]].f;
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




extern const char* reg_name[];
static int sdisreg(char* buf, char sep, int o, int n)
{
  return sprintf(buf, "%c%s(r%d)", sep, reg_name[o], n);
}

static int my_sdisasm(char* buf, uintptr_t pc, const Insn_t* o, const Insn_t* i)
{
  int n = 0;
  if (i->opcode() == Op_ZERO) {
    n += sprintf(buf, "Nothing here");
    return n;
  }
  uint32_t b = *(uint32_t*)pc;
  if (i->compressed())
    n += sprintf(buf+n, "    %04x  ", b&0xFFFF);
  else
    n += sprintf(buf+n, "%08x  ",     b);
  n += sprintf(buf+n, "%-23s", op_name[i->opcode()]);
  char sep = ' ';
  if (i->rd()  != NOREG) { n += sdisreg(buf+n, sep, o->rd(),  i->rd() ); sep=','; }
  if (i->rs1() != NOREG) { n += sdisreg(buf+n, sep, o->rs1(), i->rs1()); sep=','; }
  if (i->longimmed())    { n += sprintf(buf+n, "%c%ld", sep, i->immed()); }
  else {
    if (i->rs2() != NOREG) { n += sdisreg(buf+n, sep, o->rs2(), i->rs2()); sep=','; }
    if (i->rs3() != NOREG) { n += sdisreg(buf+n, sep, o->rs3(), i->rs3()); sep=','; }
    n += sprintf(buf+n, "%c%ld", sep, i->immed());
  }
  return n;
}

static void my_disasm(uintptr_t pc, const Insn_t* o, const Insn_t* i, const char* end, FILE* f)
{
  char buffer[1024];
  my_sdisasm(buffer, pc, o, i);
  fprintf(f, "%s%s", buffer, end);
}



void core_t::ooo_pipeline()
{
  fprintf(stderr, "ooo_pipeline() starting\n");

  // initialize register map and freelist
  for (int k=0; k<64; ++k) {
    regmap[k] = k;
    reguses[k] = 1;
  }
  for (int k=64; k<64+num_deferred_insns; ++k) {
    freelist.enque(k);
    reguses[k] = 0;
  }
  regmap[NOREG] = NOREG;
  reguses[NOREG] = 0;
  memset(regbusy, 0, sizeof regbusy);

 pc = get_state();
  Header_t* bb = find_bb(pc);
  i = insnp(bb+1);

  long cycle = 0;
  for (;;) {
#if 0
    if (conf_show()) {
      put_state();
      print(pc, i, stdout);
    }
    else if (conf_trace()) {
      fprintf(trace_file, "%lx\n", pc);
    }
#endif

    // once per cycle
    ++cycle;
    if (conf_show())
      fprintf(stderr, "\n%6ld ", cycle);

    s.reg[0].x = 0;
    
    Insn_t ir;			// instruction with physical register numbers

    // rename input registers
    uint8_t r1 = regmap[i->rs1()];
    uint8_t r2 = i->longimmed() ? NOREG : regmap[i->rs2()];
    uint8_t r3 = i->longimmed() ? NOREG : regmap[i->rs3()];

    // rename output register
    uint8_t rd = i->rd();
    if (rd != NOREG && rd != 0) {
      if (freelist.empty()) {
	if (conf_show())
	  fprintf(stderr, "unable to rename output register");
	goto issue_from_queue;	// unable to rename output register
      }
      uint8_t old_rd = regmap[rd];
      if (--reguses[old_rd] == 0)
	freelist.enque(old_rd);
      rd = freelist.deque();
      regmap[i->rd()] = rd;
      ++reguses[rd];
    }

    // create instruction with physical registers
    ir = *i;
    ir.op_rd = rd;
    ir.op_rs1 = r1;
    if (!ir.longimmed()) {
      ir.op.rs2 = r2;
      ir.op.rs3 = r3;
    }

    // new instruction ready to execute immediately?
    if (regbusy[r1] || regbusy[r2] || regbusy[r3]) {
      if (conf_show())
	fprintf(stderr, "new instruction has busy registers");
      goto enqueue_new_insn;	// input register not ready
    }
    
    // immediately execute new instruction
    {
      
      if (conf_show()) {
	fprintf(stderr, "new ");
	labelpc(pc, stderr);
	my_disasm(pc, i, &ir, "", stderr);
      }

      uintptr_t jumped = perform(&ir, pc);
      pc += ir.compressed() ? 2 : 4;
      if (jumped || ++i >= insnp(bb+1)+bb->count) {
	if (jumped)
	  pc = jumped;
	bb = find_bb(pc);
	i = insnp(bb+1);
      }
    }
    continue;

  enqueue_new_insn:
    if (!issueq.full()) {
      issueq.append({*i, ir, pc});
      pc += ir.compressed() ? 2 : 4;
      if (++i >= insnp(bb+1)+bb->count) {
	bb = find_bb(pc);
	i = insnp(bb+1);
      }
      if (conf_show())
	fprintf(stderr, "enqueued instruction");
    }
    else {
      if (conf_show())
	fprintf(stderr, "issue queue full");
    }
    
  issue_from_queue:
    iq_elt_t e;
    if (issueq.issued(e)) {
      if (conf_show()) {
	fprintf(stderr, "OLD ");
	labelpc(e.p, stderr);
	my_disasm(e.p, &e.o, &e.n, "", stderr);
      }
      (void)perform(&e.n, e.p);
    }
    else {
      if (conf_show())
	fprintf(stderr, "Nothing in queue");
    }
    
  } // once per cycle
}

void my_interpreter(core_t* c)
{
  c->ooo_pipeline();
}

void my_riscv_syscall(hart_t* h)
{
  core_t* c = (core_t*)h;
  long a0 = c->s.reg[c->regmap[10]].x;
  long a1 = c->s.reg[c->regmap[11]].x;
  long a2 = c->s.reg[c->regmap[12]].x;
  long a3 = c->s.reg[c->regmap[13]].x;
  long a4 = c->s.reg[c->regmap[14]].x;
  long a5 = c->s.reg[c->regmap[15]].x;
  long rvnum = c->s.reg[c->regmap[17]].x;
  long rv = proxy_syscall(rvnum, a0, a1, a2, a3, a4, a5, c);
  uint8_t old_rd = c->regmap[10];
  if (--c->reguses[old_rd] == 0)
    c->freelist.enque(old_rd);
  uint8_t rd = c->freelist.deque();
  c->regmap[10] = rd;
  ++c->reguses[rd];
  c->s.reg[rd].x = rv;
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
