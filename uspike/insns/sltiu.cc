#include "spike_link.h"
long I_sltiu(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(RS1 < reg_t(insn.i_imm()));
  return pc + 4;
}
