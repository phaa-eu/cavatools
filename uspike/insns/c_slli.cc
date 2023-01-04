#include "spike_link.h"
long I_c_slli(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require(insn.rvc_zimm() < xlen);
  WRITE_RD(sext_xlen(RVC_RS1 << insn.rvc_zimm()));
  return pc + 2;
}
