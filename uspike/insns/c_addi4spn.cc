#include "spike_link.h"
long I_c_addi4spn(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require(insn.rvc_addi4spn_imm() != 0);
  WRITE_RVC_RS2S(sext_xlen(RVC_SP + insn.rvc_addi4spn_imm()));
  return pc + 2;
}
