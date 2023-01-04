#include "spike_link.h"
long I_ld(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_rv64;
  WRITE_RD(MMU.load_int64(RS1 + insn.i_imm()));
  return pc + 4;
}
