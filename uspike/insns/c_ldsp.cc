#include "spike_link.h"
long I_c_ldsp(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  if (xlen == 32) {
    require_extension('F');
    require_fp;
    WRITE_FRD(f32(MMU.load_uint32(RVC_SP + insn.rvc_lwsp_imm())));
  } else { // c.ldsp
    require(insn.rvc_rd() != 0);
    WRITE_RD(MMU.load_int64(RVC_SP + insn.rvc_ldsp_imm()));
  }
  return pc + 2;
}
