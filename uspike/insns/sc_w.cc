#include "spike_link.h"
long I_sc_w(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('A');
  bool have_reservation = MMU.check_load_reservation(RS1, 4);
  if (have_reservation)
    MMU.store_uint32(RS1, RS2);
  MMU.yield_load_reservation();
  WRITE_RD(!have_reservation);
  return pc + 4;
}