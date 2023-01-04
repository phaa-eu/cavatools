#include "spike_link.h"
long I_bltu(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  long npc = pc + 4;
  if(RS1 < RS2)
    set_pc(BRANCH_TARGET);
  return npc;
}
