#include "spike_link.h"
long I_amomin_d(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('A');
  require_rv64;
  WRITE_RD(MMU.amo_uint64(RS1, [&](int64_t lhs) { return std::min(lhs, int64_t(RS2)); }));
  return pc + 4;
}
