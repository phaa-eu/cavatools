#include "spike_link.h"
long I_c_srai(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require(insn.rvc_zimm() < xlen);
  WRITE_RVC_RS1S(sext_xlen(sext_xlen(RVC_RS1S) >> insn.rvc_zimm()));
  return pc + 2;
}
