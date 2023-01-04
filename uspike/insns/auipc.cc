#include "spike_link.h"
long I_auipc(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  WRITE_RD(sext_xlen(insn.u_imm() + pc));
  return pc + 4;
}
