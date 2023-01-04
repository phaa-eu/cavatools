#include "spike_link.h"
long I_mul(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  WRITE_RD(sext_xlen(RS1 * RS2));
  return pc + 4;
}
