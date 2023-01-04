#include "spike_link.h"
long I_remuw(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('M');
  require_rv64;
  reg_t lhs = zext32(RS1);
  reg_t rhs = zext32(RS2);
  if(rhs == 0)
    WRITE_RD(sext32(lhs));
  else
    WRITE_RD(sext32(lhs % rhs));
  return pc + 4;
}
