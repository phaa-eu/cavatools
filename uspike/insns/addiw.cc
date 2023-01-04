#include "spike_link.h"
long I_addiw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_rv64;
  WRITE_RD(sext32(insn.i_imm() + RS1));
  return pc + 4;
}
