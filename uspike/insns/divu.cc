#include "spike_link.h"
long I_divu(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  reg_t lhs = zext_xlen(RS1);
  reg_t rhs = zext_xlen(RS2);
  if(rhs == 0)
    WRITE_RD(UINT64_MAX);
  else
    WRITE_RD(sext_xlen(lhs / rhs));
  return pc + 4;
}
