#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <map>

#include "options.h"
#include "caveat.h"
#include "strand.h"

std::map<long, const char*> fname; // dictionary of pc->name

option<long> conf_tcache("tcache", 1024, "Binary translation cache size in 4K pages");

long load_elf_binary(const char* file_name, int include_data);
long initialize_stack(int argc, const char** argv, const char** envp);
int elf_find_symbol(const char* name, long* begin, long* end);
const char* elf_find_pc(long pc, long* offset);

volatile strand_t* strand_t::_list =0;
volatile int strand_t::num_strands =0;

Insn_t* strand_t::tcache =0;

strand_t* strand_t::find(int tid)
{
  for (volatile strand_t* p=_list; p; p=p->_next)
    if (p->tid == tid)
      return (strand_t*)p;
  return 0;
}

void strand_t::initialize(class hart_t* h)
{
  do {  // atomically attach to list of strands
    _next = _list;
  } while (!__sync_bool_compare_and_swap(&_list, _next, this));
  int old_n;	
  do { // atomically increment thread count
    old_n = num_strands;
  } while (!__sync_bool_compare_and_swap(&num_strands, old_n, old_n+1));
  sid = old_n;			// after loop in case of race
  tid = gettid();
  hart_pointer = h;
  addresses = (long*)mmap(0, 8*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  dieif(!addresses, "Unable to mmap() addresses");
}

strand_t::strand_t(class hart_t* h, int argc, const char* argv[], const char* envp[])
{
  memset(this, 0, sizeof(strand_t));
  pc = load_elf_binary(argv[0], 1);
  xrf[2] = initialize_stack(argc, argv, envp);
  // tcache is global to all strands
  tcache = (Insn_t*)mmap(0, conf_tcache*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  dieif(!tcache, "unable to mmap() tcache");
  linkp(tcache)[0] = 0;		// this Header_t never match any pc
  linkp(tcache)[1] = 2;		// empty tcache
  initialize(h);		// do at end because there are atomic stuff in initialize()
}

strand_t::strand_t(class hart_t* h, strand_t* from)
{
  memcpy(this, from, sizeof(strand_t));
  initialize(h);
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



hart_t::hart_t(hart_t* from) { strand=new strand_t(this, from->strand); }
hart_t::hart_t(int argc, const char* argv[], const char* envp[], bool counting)
{
  strand = new strand_t(this, argc, argv, envp);
  if (counting) {
    _counters = (uint64_t*)mmap(0, conf_tcache*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    dieif(!_counters, "Unable to mmap counters array");
  }
  else
    _counters = 0;
}

Insn_t* hart_t::tcache() { return strand->tcache; }
void hart_t::interpreter(simfunc_t simulator) { strand->interpreter(simulator); }
long* hart_t::addresses() { return strand->addresses; }
hart_t* hart_t::list() { return (hart_t*)strand_t::_list->hart_pointer; }
hart_t* hart_t::next() { return (hart_t*)(strand->_next ? strand->_next->hart_pointer : 0); }
int hart_t::number() { return strand->sid; }
long hart_t::tid() { return strand->tid; }

hart_t* hart_t::find(int tid) { return strand_t::find(tid) ? strand_t::find(tid)->hart_pointer : 0; }
int hart_t::num_harts() { return strand_t::num_strands; }
void hart_t::debug_print() { strand->debug_print(); }

void hart_t::print_debug_trace()
{
#ifdef DEBUG
  strand->debug.print();
#else
  die("DEBUG not enabled!");
#endif
}

const char* func_name(long pc) { return fname.count(pc)==1 ? fname.at(pc) : 0; }


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
