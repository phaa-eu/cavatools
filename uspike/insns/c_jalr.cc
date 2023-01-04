#include "spike_link.h"
long I_c_jalr(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  long npc = pc + 2;
  require_extension('C');
  require(insn.rvc_rs1() != 0);
  reg_t tmp = npc;
  set_pc(RVC_RS1 & ~reg_t(1));
  WRITE_REG(X_RA, tmp);
  return npc;
}
