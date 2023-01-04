#include "spike_link.h"
long I_fsd(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('D');
  require_fp;
  MMU.store_uint64(RS1 + insn.s_imm(), FRS2.v[0]);
  return pc + 4;
}
