#include "spike_link.h"
long I_sw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  MMU.store_uint32(RS1 + insn.s_imm(), RS2);
  return pc + 4;
}
