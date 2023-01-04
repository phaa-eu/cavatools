#include "spike_link.h"
long I_csrrs(long pc, mmu_t& MMU, class processor_t* p) {
  insn_t insn = (long)(*(int32_t*)pc);
  bool write = insn.rs1() != 0;
  int csr = validate_csr(insn.csr(), write);
  reg_t old = p->get_csr(csr, insn, write);
  if (write) {
    p->set_csr(csr, old | RS1);
  }
  WRITE_RD(sext_xlen(old));
  serialize();
  return pc + 4;
}
