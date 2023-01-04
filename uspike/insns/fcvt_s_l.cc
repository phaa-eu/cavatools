#include "spike_link.h"
long I_fcvt_s_l(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_rv64;
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_FRD(i64_to_f32(RS1));
  set_fp_exceptions;
  return pc + 4;
}
