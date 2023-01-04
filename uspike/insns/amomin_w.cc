#include "spike_link.h"
long I_amomin_w(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('A');
  WRITE_RD(sext32(MMU.amo_uint32(RS1, [&](int32_t lhs) { return std::min(lhs, int32_t(RS2)); })));
  return pc + 4;
}
