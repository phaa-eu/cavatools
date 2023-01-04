#include "spike_link.h"
long I_addi(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(sext_xlen(RS1 + insn.i_imm()));
  return pc + 4;
}
