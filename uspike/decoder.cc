/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include "uspike.h"

Insn_t decoder(int b, long pc)
{
  Insn_t i;	       // recall all registers set to NOREG by default
#define x( lo, len) ((b >> lo) & ((1 << len)-1))
#define xs(lo, len) (b << (32-lo-len) >> (32-len))
  
#include "decoder.h"

  i.op_code = Op_UNKNOWN;
  return i;
}

#define LABEL_WIDTH  16
#define OFFSET_WIDTH  8
void labelpc(long pc, FILE* f)
{
  long offset;
  const char* label = find_pc(pc, offset);
  if (label)
    fprintf(f, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, label, -(OFFSET_WIDTH-1), offset, pc);
  else
    fprintf(f, "%*s %8lx: ", LABEL_WIDTH+OFFSET_WIDTH, "<invalid pc>", pc);
}

void disasm(long pc, const char* end, FILE* f)
{
  Insn_t i = code.at(pc);
  if (i.op_code == Op_ZERO)
    i = decoder(code.image(pc), pc);
  uint32_t b = code.image(pc);
  if (i.op_4B) fprintf(stderr, "%08x  ",     b);
  else fprintf(stderr, "    %04x  ", b&0xFFFF);
  fprintf(f, "%-23s", op_name[i.op_code]);
  char sep = ' ';
  if (i.op_rd  != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.op_rd ]); sep=','; }
  if (i.op_rs1 != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.op_rs1]); sep=','; }
  if (i.op_longimmed)    { fprintf(f, "%c%d", sep, i.op_immed); }
  else {
    if (i.op.rs2 != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.op.rs2]); sep=','; }
    if (i.op.rs3 != NOREG) { fprintf(f, "%c%s", sep, reg_name[i.op.rs3]); sep=','; }
    fprintf(f, "%c%d", sep, i.op.imm);
  }
  fprintf(f, "%s", end);
}
