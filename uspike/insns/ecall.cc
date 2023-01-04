#include "spike_link.h"
long I_ecall(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  switch (STATE.prv)
  {
    case PRV_U: throw trap_user_ecall();
    case PRV_S:
      if (STATE.v)
        throw trap_virtual_supervisor_ecall();
      else
        throw trap_supervisor_ecall();
    case PRV_M: throw trap_machine_ecall();
    default: abort();
  }
  return pc + 4;
}
