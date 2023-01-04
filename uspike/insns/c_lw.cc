#include "spike_link.h"
long I_c_lw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  WRITE_RVC_RS2S(MMU.load_int32(RVC_RS1S + insn.rvc_lw_imm()));
  return pc + 2;
}
