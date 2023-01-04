#include "spike_link.h"
long I_sh(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  MMU.store_uint16(RS1 + insn.s_imm(), RS2);
  return pc + 4;
}
