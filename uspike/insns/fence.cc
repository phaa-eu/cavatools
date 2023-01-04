#include "spike_link.h"
long I_fence(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  return pc + 4;
}
