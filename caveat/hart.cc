#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>

#include "caveat.h"
#include "hart.h"

extern "C" {
#include "specialize.h"
#include "internals.h"
};


option<size_t>	conf_tcache("tcache",	1000000L,		"Binary translation cache size");
option<size_t>	conf_hash  ("hash",	997L,			"Hash table size, best if prime number");
option<bool>	conf_show  ("show",	false, true,		"Show instruction trace");
option<>	conf_gdb   ("gdb",	0, "localhost:1234",	"Remote GDB connection");
option<bool>	conf_calls ("calls",	false, true,		"Show function calls and returns");

// in loader.cc
long emulate_execve(const char* filename, int argc, const char* argv[], const char* envp[], uintptr_t& pc);

hart_t* hart_t::_list =0;
int hart_t::_num_harts =0;

hart_t* hart_t::find(int tid)
{
  for (hart_t* p=_list; p; p=p->_next)
    if (p->tid() == tid)
      return (hart_t*)p;
  return 0;
}

void hart_t::initialize()
{
  do {  // atomically attach to list of strands
    _next = _list;
  } while (!__sync_bool_compare_and_swap(&_list, _next, this));
  sid = __sync_fetch_and_add(&_num_harts, 1);
}

hart_t::hart_t(int argc, const char* argv[], const char* envp[])
{
  memset(&s, 0, sizeof(processor_state_t));
  uintptr_t stack_pointer = emulate_execve(argv[0], argc, argv, envp, pc);
#ifdef SPIKE
  WRITE_REG(2, stack_pointer);
#else
  s.xrf[2] = stack_pointer;
#endif
  _tid = gettid();
  ptnum = pthread_self();
  simulator = 0;		// must be filled in by deriving class
  clone = 0;			// same
  syscall = default_syscall_func; // but not necessarily this one
  initialize(); // do at end because there are atomic stuff in initialize()
}

hart_t::hart_t(hart_t* from)
{
  memcpy(&s, &from->s, sizeof(processor_state_t));
  pc = from->pc;		// not in state structure
  simulator = from->simulator;
  clone = from->clone;
  syscall = from->syscall;
  initialize();
}

hart_t::~hart_t()
{
}

void hart_t::print(uintptr_t pc, Insn_t* i, FILE* out)
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




#ifdef SPIKE
#define state (*s.spike_cpu.get_state())
#else
#define state s
#endif

reg_t hart_t::get_csr(int which, insn_t insn, bool write, bool peek)
{
  switch (which) {
  case CSR_FFLAGS:
    return state.fflags;
  case CSR_FRM:
    return state.frm;
  case CSR_FCSR:
    return (state.fflags << FSR_AEXC_SHIFT) | (state.frm << FSR_RD_SHIFT);
#ifdef SPIKE
  case CSR_VCSR:
    return (p->VU.vxsat << VCSR_VXSAT_SHIFT) | (p->VU.vxrm << VCSR_VXRM_SHIFT);
#endif
  default:
    break;
  }
  die("get_csr bad number");
}

void hart_t::set_csr(int which, reg_t val)
{
#ifdef SPIKE
  dirty_fp_state;
#endif
  
  switch (which) {
  case CSR_FFLAGS:
    state.fflags = val & (FSR_AEXC >> FSR_AEXC_SHIFT);
    break;
  case CSR_FRM:
    state.frm = val & (FSR_RD >> FSR_RD_SHIFT);
    break;
  case CSR_FCSR:
    state.fflags = (val & FSR_AEXC) >> FSR_AEXC_SHIFT;
    state.frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    break;
#ifdef SPIKE
  case CSR_VCSR:
    p->VU.vxsat = (val & VCSR_VXSAT) >> VCSR_VXSAT_SHIFT;
    p->VU.vxrm = (val & VCSR_VXRM) >> VCSR_VXRM_SHIFT;
    break;
#endif
  default:
    die("set_csr bad number");
  }
}

