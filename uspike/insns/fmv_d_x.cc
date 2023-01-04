#include "spike_link.h"
long I_fmv_d_x(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_rv64;
  require_fp;
  WRITE_FRD(f64(RS1));
  return pc + 4;
}
