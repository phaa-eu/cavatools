#include "spike_link.h"
long I_fmul_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_FRD(f32_mul(f32(FRS1), f32(FRS2)));
  set_fp_exceptions;
  return pc + 4;
}
