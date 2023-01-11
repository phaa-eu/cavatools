#include "spike_link.h"
long I_c_sub(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int16_t*)pc);
  require_extension('C');
  WRITE_RVC_RS1S(sext_xlen(RVC_RS1S - RVC_RS2S));
  return pc + 2;
}