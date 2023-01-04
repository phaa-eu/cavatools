#include "spike_link.h"
long I_c_sd(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  if (xlen == 32) {
    require_extension('F');
    require_fp;
    MMU.store_uint32(RVC_RS1S + insn.rvc_lw_imm(), RVC_FRS2S.v[0]);
  } else { // c.sd
    MMU.store_uint64(RVC_RS1S + insn.rvc_ld_imm(), RVC_RS2S);
  }
  return pc + 2;
}
