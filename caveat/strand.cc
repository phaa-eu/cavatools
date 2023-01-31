#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/mman.h>

#include "options.h"
#include "caveat.h"
#include "instructions.h"
#include "strand.h"
#include "elf_loader.h"

option<long> conf_tcache("tcache", 1024, "Binary translation cache size in 4K pages");
extern option<long> conf_report;

extern "C" {
  long initialize_stack(int argc, const char** argv, const char** envp);
};

volatile strand_t* strand_t::cpu_list =0;
volatile int strand_t::num_threads =0;

strand_t* strand_t::find(int tid)
{
  for (volatile strand_t* p=list(); p; p=p->link)
    if (p->my_tid == tid)
      return (strand_t*)p;
  return 0;
}

void strand_t::print_trace(long pc, Insn_t* i)
{
  if (i->rd() == NOREG) {
    if (attributes[i->opcode()] & ATTR_st)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i->rs2()], xrf[i->rs2()]); 
    else if ((attributes[i->opcode()] & (ATTR_cj|ATTR_uj)) && (i->rs1() != NOREG))
      fprintf(stderr, "%4s[%016lx] ", reg_name[i->rs1()], xrf[i->rs1()]); 
    else if (attributes[i->opcode()] & ATTR_ex)
      fprintf(stderr, "%4s[%016lx] ", reg_name[10], xrf[10]);
    else
      fprintf(stderr, "%4s[%16s] ", "", "");
  }
  else
    fprintf(stderr, "%4s[%016lx] ", reg_name[i->rd()], xrf[i->rd()]);
  labelpc(pc);
  disasm(pc, i, "\n");
}

#ifdef DEBUG

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

void Debug_t::insert(long pc, Insn_t* i)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor].pc    = pc;
  trace[cursor].i     = i;
  trace[cursor].val   = ~0l;
}

void Debug_t::addval(reg_t val)
{
  trace[cursor].val = val;
}

void Debug_t::print()
{
  for (int k=0; k<PCTRACEBUFSZ; k++) {
    pctrace_t t = get();
    Insn_t* i = t.i;
    if (i->rd() != NOREG)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i->rd()], t.val);
    else if (attributes[i->opcode()] & ATTR_st)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i->rs2()], t.val);
    else
      fprintf(stderr, "%4s[%16s] ", "", "");
    labelpc(t.pc);
    disasm(t.pc, i, "");
    fprintf(stderr, "\n");
  }
}

#endif

void strand_t::attach_to_list()
{
  do {
    link = list();
  } while (!__sync_bool_compare_and_swap(&cpu_list, link, this));
  int old_n;			// atomically increment thread count
  do {
    old_n = num_threads;
  } while (!__sync_bool_compare_and_swap(&num_threads, old_n, old_n+1));
  _number = old_n;		// after loop in case of race
  my_tid = gettid();
}

strand_t::strand_t(class hart_t* h, int argc, const char* argv[], const char* envp[])
{
  memset(this, 0, sizeof(strand_t));
  tcache = (Insn_t*)mmap(0, conf_tcache*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  memset(tcache, 0, conf_tcache*4096);
  pc = load_elf_binary(argv[0], 1);
  xrf[2] = initialize_stack(argc, argv, envp);
  hart_pointer = h;
  addresses = (long*)mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  attach_to_list();
}

strand_t::strand_t(class hart_t* h, strand_t* from)
{
  memcpy(this, from, sizeof(strand_t));
  hart_pointer = h;
  addresses = (long*)mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  attach_to_list();
}


#define CSR_FFLAGS	0x1
#define CSR_FRM		0x2
#define CSR_FCSR	0x3

long strand_t::get_csr(int what)
{
  switch (what) {
  case CSR_FFLAGS:
    return fcsr.f.flags;
  case CSR_FRM:
    return fcsr.f.rm;
  case CSR_FCSR:
    return fcsr.ui;
  default:
    die("unsupported CSR number %d", what);
  }
}

void strand_t::set_csr(int what, long val)
{
  switch (what) {
  case CSR_FFLAGS:
    fcsr.f.flags = val;
    break;
  case CSR_FRM:
    fcsr.f.rm = val;
    break;
  case CSR_FCSR:
    fcsr.ui = val;
    break;
  default:
    die("unsupported CSR number %d", what);
  }
}

void strand_t::set_tid() { my_tid = gettid(); }

hart_t::hart_t(hart_t* from)
{
  strand = new strand_t(this, from->strand);
  next_report = conf_report;
}

hart_t::hart_t(int argc, const char* argv[], const char* envp[])
{
  strand = new strand_t(this, argc, argv, envp);
  next_report = conf_report;
}

//void hart_t::interpreter(simfunc_t f, statfunc_t s)	{ strand->interpreter(f, s); }

void hart_t::interpreter() { strand->interpreter(); }
long hart_t::executed()	{ return _executed; }
long hart_t::total_count()
{
  long total = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next())
    total += p->executed();
  return total;
}
hart_t* hart_t::list() { return strand_t::list() ? strand_t::list()->hart() : 0; }
hart_t* hart_t::next() { return strand->next() ? strand->next()->hart() : 0; }
int hart_t::number() { return strand->number(); }
long hart_t::tid() { return strand->tid(); }
void hart_t::set_tid() { strand->set_tid(); }
hart_t* hart_t::find(int tid) { return strand_t::find(tid) ? strand_t::find(tid)->hart() : 0; }
int hart_t::threads() { return strand_t::threads(); }
void hart_t::debug_print() { strand->debug_print(); }


/*
void hart_t::my_report(long total)
{
  fprintf(stderr, "%1ld%%", 100*executed()/total);
}
*/


void hart_t::print_debug_trace()
{
#ifdef DEBUG
  strand->debug.print();
#else
  die("DEBUG not enabled!");
#endif
}



#define LABEL_WIDTH  16
#define OFFSET_WIDTH  8
int slabelpc(char* buf, long pc)
{
  long offset;
  const char* label = elf_find_pc(pc, &offset);
  if (label)
    return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, label, -(OFFSET_WIDTH-1), offset, pc);
  else
    return sprintf(buf, "%*s %8lx: ", LABEL_WIDTH+OFFSET_WIDTH, "<invalid pc>", pc);
}

void labelpc(long pc, FILE* f)
{
  char buffer[1024];
  slabelpc(buffer, pc);
  fprintf(f, "%s", buffer);
}

int sdisasm(char* buf, long pc, Insn_t* i)
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
  if (i->rd()  != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rd() ]); sep=','; }
  if (i->rs1() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs1()]); sep=','; }
  if (i->longimmed())    { n += sprintf(buf+n, "%c%ld", sep, i->immed()); }
  else {
    if (i->rs2() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs2()]); sep=','; }
    if (i->rs3() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs3()]); sep=','; }
    n += sprintf(buf+n, "%c%ld", sep, i->immed());
  }
  return n;
}

void disasm(long pc, Insn_t* i, const char* end, FILE* f)
{
  char buffer[1024];
  sdisasm(buffer, pc, i);
  fprintf(f, "%s%s", buffer, end);
}
