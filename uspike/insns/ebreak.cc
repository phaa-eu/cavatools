#include "spike_link.h"
long I_ebreak(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  throw trap_breakpoint(pc);
  return pc + 4;
}
