#include "spike_link.h"
long I_fsgnjn_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  WRITE_FRD(fsgnj64(FRS1, FRS2, true, false));
  return pc + 4;
}
