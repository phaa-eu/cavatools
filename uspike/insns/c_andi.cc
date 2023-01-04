#include "spike_link.h"
long I_c_andi(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  WRITE_RVC_RS1S(RVC_RS1S & insn.rvc_imm());
  return pc + 2;
}
