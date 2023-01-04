#include "spike_link.h"
long I_mulh(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  if (xlen == 64)
    WRITE_RD(mulh(RS1, RS2));
  else
    WRITE_RD(sext32((sext32(RS1) * sext32(RS2)) >> 32));
  return pc + 4;
}
