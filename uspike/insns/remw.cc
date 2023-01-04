#include "spike_link.h"
long I_remw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  require_rv64;
  sreg_t lhs = sext32(RS1);
  sreg_t rhs = sext32(RS2);
  if(rhs == 0)
    WRITE_RD(lhs);
  else
    WRITE_RD(sext32(lhs % rhs));
  return pc + 4;
}
