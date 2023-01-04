#include "spike_link.h"
long I_c_j(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  long npc = pc + 2;
  require_extension('C');
  set_pc(pc + insn.rvc_j_imm());
  return npc;
}
