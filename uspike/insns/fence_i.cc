#include "spike_link.h"
long I_fence_i(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  MMU.flush_icache();
  return pc + 4;
}
