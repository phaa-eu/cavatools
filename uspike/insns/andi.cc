#include "spike_link.h"
long I_andi(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(insn.i_imm() & RS1);
  return pc + 4;
}
