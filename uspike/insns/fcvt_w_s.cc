#include "spike_link.h"
long I_fcvt_w_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_RD(sext32(f32_to_i32(f32(FRS1), RM, true)));
  set_fp_exceptions;
  return pc + 4;
}
