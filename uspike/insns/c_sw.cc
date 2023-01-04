#include "spike_link.h"
long I_c_sw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  MMU.store_uint32(RVC_RS1S + insn.rvc_lw_imm(), RVC_RS2S);
  return pc + 2;
}
