#include "spike_link.h"
long I_jal(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  long npc = pc + 4;
  reg_t tmp = npc;
  set_pc(JUMP_TARGET);
  WRITE_RD(tmp);
  return npc;
}
