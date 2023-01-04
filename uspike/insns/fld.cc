#include "spike_link.h"
long I_fld(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  WRITE_FRD(f64(MMU.load_uint64(RS1 + insn.i_imm())));
  return pc + 4;
}
