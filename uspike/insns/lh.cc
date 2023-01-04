#include "spike_link.h"
long I_lh(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(MMU.load_int16(RS1 + insn.i_imm()));
  return pc + 4;
}
