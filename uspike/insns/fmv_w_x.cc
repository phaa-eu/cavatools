#include "spike_link.h"
long I_fmv_w_x(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  WRITE_FRD(f32(RS1));
  return pc + 4;
}
