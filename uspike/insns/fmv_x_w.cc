#include "spike_link.h"
long I_fmv_x_w(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  WRITE_RD(sext32(FRS1.v[0]));
  return pc + 4;
}
