#include "spike_link.h"
long I_c_lwsp(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require(insn.rvc_rd() != 0);
  WRITE_RD(MMU.load_int32(RVC_SP + insn.rvc_lwsp_imm()));
  return pc + 2;
}
