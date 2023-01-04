#include "spike_link.h"
long I_c_swsp(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  MMU.store_uint32(RVC_SP + insn.rvc_swsp_imm(), RVC_RS2);
  return pc + 2;
}
