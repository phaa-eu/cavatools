#include "spike_link.h"
long I_c_fldsp(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  require_extension('D');
  require_fp;
  WRITE_FRD(f64(MMU.load_uint64(RVC_SP + insn.rvc_ldsp_imm())));
  return pc + 2;
}
