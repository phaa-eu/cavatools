#include "spike_link.h"
long I_xor(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(RS1 ^ RS2);
  return pc + 4;
}
