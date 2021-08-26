/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdio.h>

#include "cpu.h"
#include "uspike.h"

Insn_t decoder(int b, long pc)
{
  Insn_t i(Op_UNKNOWN);	       // recall all registers set to NOREG by default
#define x( lo, len) ((b >> lo) & ((1 << len)-1))
#define xs(lo, len) (b << (32-lo-len) >> (32-len))
  
#include "decoder.h"
  
 opcode_found:
  return i;
}

#define LABEL_WIDTH  16
#define OFFSET_WIDTH  8
void labelpc(long pc, FILE* f)
{
  long offset;
  const char* label = elf_find_pc(pc, &offset);
  if (label)
    fprintf(f, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, label, -(OFFSET_WIDTH-1), offset, pc);
  else
    fprintf(f, "%*s %8lx: ", LABEL_WIDTH+OFFSET_WIDTH, "<invalid pc>", pc);
}

void disasm(long pc, const char* end, FILE* f)
{
  Insn_t i = code.at(pc);
  if (i.opcode() == Op_ZERO)
    i = decoder(code.image(pc), pc);
  uint32_t b = code.image(pc);
  if (i.compressed())
    fprintf(stderr, "    %04x  ", b&0xFFFF);
  else
    fprintf(stderr, "%08x  ",     b);
  fprintf(f, "%-23s", op_name[i.opcode()]);
  char sep = ' ';
  if (i.rd()  != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.rd() ]); sep=','; }
  if (i.rs1() != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.rs1()]); sep=','; }
  if (i.longimmed())    { fprintf(f, "%c%ld", sep, i.immed()); }
  else {
    if (i.rs2() != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.rs2()]); sep=','; }
    if (i.rs3() != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.rs3()]); sep=','; }
    fprintf(f, "%c%ld", sep, i.immed());
  }
  fprintf(f, "%s", end);
}
