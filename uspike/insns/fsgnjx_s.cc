#include "spike_link.h"
long I_fsgnjx_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  WRITE_FRD(fsgnj32(FRS1, FRS2, false, true));
  return pc + 4;
}
