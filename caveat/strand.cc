#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>

#include "options.h"
#include "caveat.h"
#include "strand.h"

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
