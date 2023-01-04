#include "spike_link.h"
long I_div(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  sreg_t lhs = sext_xlen(RS1);
  sreg_t rhs = sext_xlen(RS2);
  if(rhs == 0)
    WRITE_RD(UINT64_MAX);
  else if(lhs == INT64_MIN && rhs == -1)
    WRITE_RD(lhs);
  else
    WRITE_RD(sext_xlen(lhs / rhs));
  return pc + 4;
}
