#include "spike_link.h"
long I_lr_w(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('A');
  auto res = MMU.load_int32(RS1, true);
  MMU.acquire_load_reservation(RS1);
  WRITE_RD(res);
  return pc + 4;
}
