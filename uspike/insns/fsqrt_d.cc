#include "spike_link.h"
long I_fsqrt_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_FRD(f64_sqrt(f64(FRS1)));
  set_fp_exceptions;
  return pc + 4;
}
