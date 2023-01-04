#include "spike_link.h"
long I_c_add(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require(insn.rvc_rs2() != 0);
  WRITE_RD(sext_xlen(RVC_RS1 + RVC_RS2));
  return pc + 2;
}
