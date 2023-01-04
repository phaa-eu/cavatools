#include "spike_link.h"
long I_feq_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  WRITE_RD(f64_eq(f64(FRS1), f64(FRS2)));
  set_fp_exceptions;
  return pc + 4;
}
