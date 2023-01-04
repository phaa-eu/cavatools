#include "spike_link.h"
long I_c_addi(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  WRITE_RD(sext_xlen(RVC_RS1 + insn.rvc_imm()));
  return pc + 2;
}
