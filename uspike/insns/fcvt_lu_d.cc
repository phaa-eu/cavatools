#include "spike_link.h"
long I_fcvt_lu_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_rv64;
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_RD(f64_to_ui64(f64(FRS1), RM, true));
  set_fp_exceptions;
  return pc + 4;
}
