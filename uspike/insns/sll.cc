#include "spike_link.h"
long I_sll(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(sext_xlen(RS1 << (RS2 & (xlen-1))));
  return pc + 4;
}
