#include "spike_link.h"
long I_c_mv(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require(insn.rvc_rs2() != 0);
  WRITE_RD(RVC_RS2);
  return pc + 2;
}
