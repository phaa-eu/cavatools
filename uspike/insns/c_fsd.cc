#include "spike_link.h"
long I_c_fsd(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require_extension('D');
  require_fp;
  MMU.store_uint64(RVC_RS1S + insn.rvc_ld_imm(), RVC_FRS2S.v[0]);
  return pc + 2;
}
