#include "spike_link.h"
long I_sllw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_rv64;
  WRITE_RD(sext32(RS1 << (RS2 & 0x1F)));
  return pc + 4;
}
