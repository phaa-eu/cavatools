#include "spike_link.h"
long I_c_sdsp(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  if (xlen == 32) {
    require_extension('F');
    require_fp;
    MMU.store_uint32(RVC_SP + insn.rvc_swsp_imm(), RVC_FRS2.v[0]);
  } else { // c.sdsp
    MMU.store_uint64(RVC_SP + insn.rvc_sdsp_imm(), RVC_RS2);
  }
  return pc + 2;
}
