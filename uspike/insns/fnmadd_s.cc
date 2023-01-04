#include "spike_link.h"
long I_fnmadd_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  softfloat_roundingMode = RM;
  WRITE_FRD(f32_mulAdd(f32(f32(FRS1).v ^ F32_SIGN), f32(FRS2), f32(f32(FRS3).v ^ F32_SIGN)));
  set_fp_exceptions;
  return pc + 4;
}
