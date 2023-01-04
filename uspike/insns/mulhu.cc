#include "spike_link.h"
long I_mulhu(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  if (xlen == 64)
    WRITE_RD(mulhu(RS1, RS2));
  else
    WRITE_RD(sext32(((uint64_t)(uint32_t)RS1 * (uint64_t)(uint32_t)RS2) >> 32));
  return pc + 4;
}
