#include "spike_link.h"
long I_csrrwi(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  int csr = validate_csr(insn.csr(), true);
  reg_t old = p->get_csr(csr, insn, true);
  p->set_csr(csr, insn.rs1());
  WRITE_RD(sext_xlen(old));
  serialize();
  return pc + 4;
}
