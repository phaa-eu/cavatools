#include "spike_link.h"
long I_fcvt_w_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_RD(sext32(f64_to_i32(f64(FRS1), RM, true)));
  set_fp_exceptions;
  return pc + 4;
}
