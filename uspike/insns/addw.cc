#include "spike_link.h"
long I_addw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_rv64;
  WRITE_RD(sext32(RS1 + RS2));
  return pc + 4;
}
