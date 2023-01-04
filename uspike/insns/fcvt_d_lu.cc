#include "spike_link.h"
long I_fcvt_d_lu(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_rv64;
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_FRD(ui64_to_f64(RS1));
  set_fp_exceptions;
  return pc + 4;
}
