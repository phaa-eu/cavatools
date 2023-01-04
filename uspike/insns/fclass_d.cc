#include "spike_link.h"
long I_fclass_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  WRITE_RD(f64_classify(f64(FRS1)));
  return pc + 4;
}
