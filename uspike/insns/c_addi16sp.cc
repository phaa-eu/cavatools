#include "spike_link.h"
long I_c_addi16sp(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  if (insn.rvc_rd() == 2) { // c.addi16sp
    require(insn.rvc_addi16sp_imm() != 0);
    WRITE_REG(X_SP, sext_xlen(RVC_SP + insn.rvc_addi16sp_imm()));
  } else {
    require(insn.rvc_imm() != 0);
    WRITE_RD(insn.rvc_imm() << 12);
  }
  return pc + 2;
}
