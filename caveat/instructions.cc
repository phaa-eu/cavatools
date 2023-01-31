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

  return Insn_t(Op_ILLEGAL, 0, 0);
}

#include "constants.h"
