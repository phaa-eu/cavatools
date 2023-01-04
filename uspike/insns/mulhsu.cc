#include "spike_link.h"
long I_mulhsu(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  if (xlen == 64)
    WRITE_RD(mulhsu(RS1, RS2));
  else
    WRITE_RD(sext32((sext32(RS1) * reg_t((uint32_t)RS2)) >> 32));
  return pc + 4;
}
