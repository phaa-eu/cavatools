#include "spike_link.h"
long I_fmax_s(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  require_extension('F');
  require_fp;
  bool greater = f32_lt_quiet(f32(FRS2), f32(FRS1)) ||
                 (f32_eq(f32(FRS2), f32(FRS1)) && (f32(FRS2).v & F32_SIGN));
  if (isNaNF32UI(f32(FRS1).v) && isNaNF32UI(f32(FRS2).v))
    WRITE_FRD(f32(defaultNaNF32UI));
  else
    WRITE_FRD(greater || isNaNF32UI(f32(FRS2).v) ? FRS1 : FRS2);
  set_fp_exceptions;
  return pc + 4;
}
