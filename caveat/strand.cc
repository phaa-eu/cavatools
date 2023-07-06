#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>

#include <string>
#include <map>

#include "options.h"
#include "caveat.h"
#include "strand.h"

extern option<long> conf_tcache;

std::map<long, std::string> fname; // dictionary of pc->name

const char* func_name(uintptr_t pc) { return fname.count(pc)==1 ? const_cast<const char*>(fname.at(pc).c_str()) : "NOT FOUND"; }

long emulate_execve(const char* filename, int argc, const char* argv[], const char* envp[], uintptr_t& pc);

volatile strand_t* strand_t::_list =0;
volatile int strand_t::num_strands =0;

strand_t* strand_t::find(int tid)
{
  for (volatile strand_t* p=_list; p; p=p->_next)
    if (p->tid == tid)
      return (strand_t*)p;
  return 0;
}

void strand_t::initialize(class hart_base_t* h)
{
  do {  // atomically attach to list of strands
    _next = _list;
  } while (!__sync_bool_compare_and_swap(&_list, _next, this));
  int old_n;	
  do { // atomically increment thread count
    old_n = num_strands;
  } while (!__sync_bool_compare_and_swap(&num_strands, old_n, old_n+1));
  sid = old_n;			// after loop in case of race
  hart_pointer = h;
}

strand_t::strand_t(class hart_base_t* h, int argc, const char* argv[], const char* envp[])
{
  memset(&s, 0, sizeof(processor_state_t));
  long stack_pointer;
  s.xrf[2] = emulate_execve(argv[0], argc, argv, envp, pc);
  tid = gettid();
  ptnum = pthread_self();
  initialize(h); // do at end because there are atomic stuff in initialize()
  extern int maintid;
  maintid = tid;
}

strand_t::strand_t(class hart_base_t* h, strand_t* from)
{
  memcpy(&s, &from->s, sizeof(processor_state_t));
  pc = from->pc;		// not in state
  initialize(h);
}

#define CSR_FFLAGS	0x1
#define CSR_FRM		0x2
#define CSR_FCSR	0x3

long strand_t::get_csr(int what)
{
  switch (what) {
  case CSR_FFLAGS:
    return s.fcsr.f.flags;
  case CSR_FRM:
    return s.fcsr.f.rm;
  case CSR_FCSR:
    return s.fcsr.ui;
  default:
    die("unsupported CSR number %d", what);
  }
}

void strand_t::set_csr(int what, long val)
{
  switch (what) {
  case CSR_FFLAGS:
    s.fcsr.f.flags = val;
    break;
  case CSR_FRM:
    s.fcsr.f.rm = val;
    break;
  case CSR_FCSR:
    s.fcsr.ui = val;
    break;
  default:
    die("unsupported CSR number %d", what);
  }
}



void Tcache_t::initialize(size_t cachesize, size_t hashtablesize)
{
  _extent = cachesize;
  _hashsize = hashtablesize;
#if 0
  array = new uint64_t[_extent];
  map = new Header_t*[_hashsize];
#else
  array = (uint64_t*)mmap(0, _extent*sizeof(uint64_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  table = (Header_t**)mmap(0, _hashsize*sizeof(uint64_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#endif
  list = 0;
  clear();
  
  //running = 0;
  //pause = false;
}

Header_t* Tcache_t::find(uintptr_t pc)
{
  Header_t* h = table[hashfunction(pc)];
  while (h) {
    if (h->addr == pc)
      return h;
    h = h->next;
  }
  return 0;
}

void Tcache_t::insert(Header_t* bb)
{
  size_t k = hashfunction(bb->addr);
  bb->next = table[k];
  table[k] = bb;
}

void Tcache_t::clear()
{
  //  memset((void*)array, 0, _extent*sizeof(uint64_t));
  memset((void*)table, 0, _hashsize*sizeof(Header_t**));
  _size = 0;
}

Header_t* Tcache_t::add(Header_t* begin, size_t entries)
{
  dieif(_size+entries>_extent, "Tcache::add() _size=%ld + %ld _extent=%ld", _size, entries, _extent);

  __sync_fetch_and_add(&_size, entries);
  
  uintptr_t* before = array + _size;
  memcpy(before, begin, entries*sizeof(uint64_t));
  _size += entries;
  return (Header_t*)before;
}




hart_base_t::hart_base_t(hart_base_t* from)
{
  strand = new strand_t(this, from->strand);
  simulator = from->simulator;
  clone = from->clone;
  syscall = from->syscall;
}

hart_base_t::hart_base_t(int argc, const char* argv[], const char* envp[])
{
  strand = new strand_t(this, argc, argv, envp);
  syscall = default_syscall_func;
}

bool hart_base_t::interpreter() { return strand->interpreter(); }
bool hart_base_t::single_step(bool show_trace) { return strand->single_step(show_trace); }
hart_base_t* hart_base_t::list() { return (hart_base_t*)strand_t::_list->hart_pointer; }
hart_base_t* hart_base_t::next() { return (hart_base_t*)(strand->_next ? strand->_next->hart_pointer : 0); }
int hart_base_t::number() { return strand->sid; }
int hart_base_t::tid() { return strand->tid; }
uintptr_t hart_base_t::pc() { return strand->pc; }

hart_base_t* hart_base_t::find(int tid) { return strand_t::find(tid) ? strand_t::find(tid)->hart_pointer : 0; }
int hart_base_t::num_harts() { return strand_t::num_strands; }
void hart_base_t::debug_print() { strand->debug_print(); }


void hart_base_t::print_debug_trace()
{
#ifdef DEBUG
  strand->debug.print();
#else
  die("DEBUG not enabled!");
#endif
}


#define LABEL_WIDTH  16
#define OFFSET_WIDTH  8
int slabelpc(char* buf, uintptr_t pc)
{
  auto it = fname.upper_bound(pc);
  if (it == fname.end()) {
    return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, "NONE", -(OFFSET_WIDTH-1), 0L, pc);
  }
  else {
    it--;
    if (it == fname.end()) {
      return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, "UNKNOWN", -(OFFSET_WIDTH-1), 0L, pc);
    }
    else {
      long offset = pc - it->first;
      const char* name = const_cast<const char*>(it->second.c_str());
      return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, name, -(OFFSET_WIDTH-1), offset, pc);
    }
  }
}

void labelpc(uintptr_t pc, FILE* f)
{
  char buffer[1024];
  slabelpc(buffer, pc);
  fprintf(f, "%s", buffer);
}

int sdisasm(char* buf, uintptr_t pc, const Insn_t* i)
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

void disasm(uintptr_t pc, const Insn_t* i, const char* end, FILE* f)
{
  char buffer[1024];
  sdisasm(buffer, pc, i);
  fprintf(f, "%s%s", buffer, end);
}

void strand_t::print_trace(uintptr_t pc, Insn_t* i, FILE* out)
{
  fprintf(out, "[%d] ", gettid());
  if (i->rd() == NOREG) {
    if (attributes[i->opcode()] & ATTR_st)
      fprintf(out, "%4s[%016lx] ", reg_name[i->rs2()], s.xrf[i->rs2()]); 
    else if ((attributes[i->opcode()] & (ATTR_cj|ATTR_uj)) && (i->rs1() != NOREG))
      fprintf(out, "%4s[%016lx] ", reg_name[i->rs1()], s.xrf[i->rs1()]); 
    else if (attributes[i->opcode()] & ATTR_ex)
      fprintf(out, "%4s[%016lx] ", reg_name[10], s.xrf[10]);
    else
      fprintf(out, "%4s[%16s] ", "", "");
  }
  else
    fprintf(out, "%4s[%016lx] ", reg_name[i->rd()], s.xrf[i->rd()]);
  labelpc(pc, stdout);
  disasm(pc, i, "\n", stdout);
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

void Debug_t::insert(long pc, Insn_t i)
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
    Insn_t i = t.i;
    if (i.rd() != NOREG)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i.rd()], t.val);
    else if (attributes[i.opcode()] & ATTR_st)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i.rs2()], t.val);
    else
      fprintf(stderr, "%4s[%16s] ", "", "");
    labelpc(t.pc);
    disasm(t.pc, &i, "");
    fprintf(stderr, "\n");
  }
}

#endif
