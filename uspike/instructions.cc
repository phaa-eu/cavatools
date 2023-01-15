/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdint.h>
#include <stdio.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"
#include "elf_loader.h"

insnSpace_t code;

void insnSpace_t::loadelf(const char* elfname)
{
  _entry = load_elf_binary(elfname, 1);
  _base=low_bound;
  _limit=high_bound;
  int n = (_limit - _base) / 2;
  predecoded=new Insn_t[n];
  memset(predecoded, 0, n*sizeof(Insn_t));
  // Predecode instruction code segment
  long pc = _base;
  while (pc < _limit) {
    Insn_t i = code.set(pc, decoder(code.image(pc), pc));
    pc += i.compressed() ? 2 : 4;
  }
  substitute_cas(_base, _limit);
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

Insn_t decoder(int b, long pc)
{
#define x( lo, len) ((b >> lo) & ((1 << len)-1))
#define xs(lo, len) (b << (32-lo-len) >> (32-len))
  
#include "decoder.h"

  die("Unknown opcode")
}

void redecode(long pc)
{
  if (code.valid(pc))
    code.set(pc, decoder(code.image(pc), pc));
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

int sdisasm(char* buf, long pc)
{
  Insn_t i = code.at(pc);
  if (i.opcode() == Op_ZERO)
    i = decoder(code.image(pc), pc);
  uint32_t b = code.image(pc);
  int n = 0;
  if (i.compressed())
    n += sprintf(buf+n, "    %04x  ", b&0xFFFF);
  else
    n += sprintf(buf+n, "%08x  ",     b);
  n += sprintf(buf+n, "%-23s", op_name[i.opcode()]);
  char sep = ' ';
  if (i.rd()  != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i.rd() ]); sep=','; }
  if (i.rs1() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i.rs1()]); sep=','; }
  if (i.longimmed())    { n += sprintf(buf+n, "%c%ld", sep, i.immed()); }
  else {
    if (i.rs2() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i.rs2()]); sep=','; }
    if (i.rs3() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i.rs3()]); sep=','; }
    n += sprintf(buf+n, "%c%ld", sep, i.immed());
  }
  return n;
}

void disasm(long pc, const char* end, FILE* f)
{
  char buffer[1024];
  sdisasm(buffer, pc);
  fprintf(f, "%s%s", buffer, end);
}

void substitute_cas(long lo, long hi)
{
  // look for compare-and-swap pattern
  long possible=0, replaced=0;
  for (long pc=lo; pc<hi; pc+=code.at(pc).compressed()?2:4) {
    Insn_t i = code.at(pc);
    if (!(i.opcode() == Op_lr_w || i.opcode() == Op_lr_d))
      continue;
    possible++;
    Insn_t i2 = code.at(pc+4);
    if (i2.opcode() != Op_bne && i2.opcode() != Op_c_bnez) continue;
    int len = 4 + (i2.opcode()==Op_c_bnez ? 2 : 4);
    Insn_t i3 = code.at(pc+len);
    if (i3.opcode() != Op_sc_w && i3.opcode() != Op_sc_d) continue;
    // pattern found, check registers
    int load_reg = i.rd();
    int addr_reg = i.rs1();
    int test_reg = (i2.opcode() == Op_c_bnez) ? 0 : i2.rs2();
    int newv_reg = i3.rs2();
    int flag_reg = i3.rd();
    if (i2.rs1() != load_reg) continue;
    if (i3.rs1() != addr_reg) continue;
    // pattern is good
    Opcode_t op;
    if (len == 8) op = (i.opcode() == Op_lr_w) ? Op_cas12_w : Op_cas12_d;
    else          op = (i.opcode() == Op_lr_w) ? Op_cas10_w : Op_cas10_d;
    code.set(pc, Insn_t(op, flag_reg, addr_reg, test_reg, newv_reg, i2.immed()));
    replaced++;
  }
  if (replaced != possible) {
    fprintf(stderr, "%ld Load-Reserve found, %ld substitution failed\n", possible, possible-replaced);
    exit(-1);
  }
}

#include "constants.h"
