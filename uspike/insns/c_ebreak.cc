#include "spike_link.h"
long I_c_ebreak(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  throw trap_breakpoint(pc);
  return pc + 2;
}
