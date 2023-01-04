#include "spike_link.h"
long I_feq_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  WRITE_RD(f32_eq(f32(FRS1), f32(FRS2)));
  set_fp_exceptions;
  return pc + 4;
}
