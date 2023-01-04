#include "spike_link.h"
long I_lb(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(MMU.load_int8(RS1 + insn.i_imm()));
  return pc + 4;
}
