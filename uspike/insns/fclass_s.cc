#include "spike_link.h"
long I_fclass_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  WRITE_RD(f32_classify(f32(FRS1)));
  return pc + 4;
}
