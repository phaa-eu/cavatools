#include "spike_link.h"
long I_lui(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(insn.u_imm());
  return pc + 4;
}
