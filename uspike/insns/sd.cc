#include "spike_link.h"
long I_sd(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_rv64;
  MMU.store_uint64(RS1 + insn.s_imm(), RS2);
  return pc + 4;
}
