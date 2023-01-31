/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "options.h"
#include "caveat.h"
#include "instructions.h"
#include "strand.h"
#include "elf_loader.h"

extern "C" {
  void redecode(long pc);
};

insnSpace_t code;

option<long> conf_tcache("tcache", 1024, "Binary translation cache size in 4K pages");

void insnSpace_t::loadelf(const char* elfname)
{
  tcache = (Isegment_t*)mmap(0, conf_tcache*4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  dieif((uint64_t)tcache & (4096-1), "tcache not 4K page aligned");
  tcache->end = (Insn_t*)tcache + 4096/sizeof(Insn_t);
  _entry = load_elf_binary(elfname, 1);
  return;
}


Insn_t strand_t::substitute_cas(Insn_t* i3)
{
  fprintf(stderr, "substitute_cas(pc=%lx)\n", pc);
  dieif(i3->opcode()!=Op_sc_w && i3->opcode()!=Op_sc_d, "0x%lx no SC found in substitute_cas()", pc);
  int blen = 4;
  Insn_t i2 = decoder(pc-blen);
  if (i2.opcode() != Op_bne) {
    blen = 2;
    i2 = decoder(pc-blen);
  }
  dieif(i2.opcode()!=Op_bne && i2.opcode()!=Op_c_bnez, "0x%lx instruction before SC not bne/bnez", pc);
  Insn_t i1 = decoder(pc-blen-4);
  dieif(i1.opcode()!=Op_lr_w && i1.opcode()!=Op_lr_d, "0x%lx substitute_cas called without LR", pc);
  // pattern found, check registers
  int load_reg = i1.rd();
  //  int addr_reg = i1.rs1();
  int addr_reg = i3->rs1();
  int test_reg = (i2.opcode() == Op_c_bnez) ? 0 : i2.rs2();
  int newv_reg = i3->rs2();
  int flag_reg = i3->rd();
  dieif(i1.rs1()!=addr_reg || i2.rs1()!=load_reg, "0x%lx CAS pattern incorrect registers", pc);
  // pattern is good
  //  *i3 = Insn_t(i3->opcode()==Op_sc_w ? Op_cas_w : Op_cas_d, flag_reg, addr_reg, test_reg, newv_reg, i2.immed()-blen);
  return Insn_t(i3->opcode()==Op_sc_w?Op_cas_w:Op_cas_d, flag_reg, addr_reg, newv_reg, test_reg, i3->immed());
}


Insn_t::Insn_t(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int8_t rs3, int16_t imm)
{
  op_code = code;
  op_rd = rd;
  op_rs1 = rs1;
  op.rs2 = rs2;
  op.rs3 = rs3;
  setimm(imm);
}

Insn_t::Insn_t(Opcode_t code, int8_t rd, int8_t rs1, int8_t rs2, int16_t imm)
{
  op_code = code;
  op_rd = rd;
  op_rs1 = rs1;
  op.rs2 = rs2;
  op.rs3 = NOREG;
  setimm(imm);
}

Insn_t::Insn_t(Opcode_t code, int8_t rd, int8_t rs1, int32_t longimmed)
{
  op_code = code;
  op_rd = rd;
  op_rs1 = rs1;
  op_longimm = longimmed;
}

Insn_t::Insn_t(Opcode_t code, int8_t rd, int32_t longimmed)
{
  op_code = code;
  op_rd = rd;
  op_rs1 = NOREG;
  op_longimm = longimmed;
}











Insn_t decoder(long pc)
{
  int32_t b = *(int32_t*)pc;
  
#define x( lo, len) ((b >> lo) & ((1 << len)-1))
#define xs(lo, len) (b << (32-lo-len) >> (32-len))
  
#include "decoder.h"

  die("Unknown opcode at 0x%lx", pc)
}

#define LABEL_WIDTH  16
#define OFFSET_WIDTH  8
int slabelpc(char* buf, long pc)
{
  long offset;
  const char* label = elf_find_pc(pc, &offset);
  if (label)
    return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, label, -(OFFSET_WIDTH-1), offset, pc);
  else
    return sprintf(buf, "%*s %8lx: ", LABEL_WIDTH+OFFSET_WIDTH, "<invalid pc>", pc);
}

void labelpc(long pc, FILE* f)
{
  char buffer[1024];
  slabelpc(buffer, pc);
  fprintf(f, "%s", buffer);
}

int sdisasm(char* buf, long pc, Insn_t* i)
{
  int n = 0;
  if (i->opcode() == Op_ZERO) {
    n += sprintf(buf, "Nothing here");
    return n;
  }
  uint32_t b = *(uint32_t*)pc;
  if (i->compressed())
    n += sprintf(buf+n, "    %04x  ", b&0xFFFF);
  else
    n += sprintf(buf+n, "%08x  ",     b);
  n += sprintf(buf+n, "%-23s", op_name[i->opcode()]);
  char sep = ' ';
  if (i->rd()  != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rd() ]); sep=','; }
  if (i->rs1() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs1()]); sep=','; }
  if (i->longimmed())    { n += sprintf(buf+n, "%c%ld", sep, i->immed()); }
  else {
    if (i->rs2() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs2()]); sep=','; }
    if (i->rs3() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs3()]); sep=','; }
    n += sprintf(buf+n, "%c%ld", sep, i->immed());
  }
  return n;
}

void disasm(long pc, Insn_t* i, const char* end, FILE* f)
{
  char buffer[1024];
  sdisasm(buffer, pc, i);
  fprintf(f, "%s%s", buffer, end);
}

#include "constants.h"
