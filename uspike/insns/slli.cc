#include "spike_link.h"
long I_slli(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require(SHAMT < xlen);
  WRITE_RD(sext_xlen(RS1 << SHAMT));
  return pc + 4;
}
