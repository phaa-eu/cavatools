#include "spike_link.h"
long I_slt(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(sreg_t(RS1) < sreg_t(RS2));
  return pc + 4;
}
