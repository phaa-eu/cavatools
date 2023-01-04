#include "spike_link.h"
long I_c_beqz(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  long npc = pc + 2;
  require_extension('C');
  if (RVC_RS1S == 0)
    set_pc(pc + insn.rvc_b_imm());
  return npc;
}
