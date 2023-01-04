#include "spike_link.h"
long I_srliw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_rv64;
  WRITE_RD(sext32((uint32_t)RS1 >> SHAMT));
  return pc + 4;
}
