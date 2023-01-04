#include "spike_link.h"
long I_dret(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  long npc = pc + 4;
  require(STATE.debug_mode);
  set_pc_and_serialize(STATE.dpc);
  p->set_privilege(STATE.dcsr.prv);
  /* We're not in Debug Mode anymore. */
  STATE.debug_mode = false;
  if (STATE.dcsr.step)
    STATE.single_step = STATE.STEP_STEPPING;
  return npc;
}
