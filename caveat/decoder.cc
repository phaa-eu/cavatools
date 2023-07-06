/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "caveat.h"


Insn_t decoder(uintptr_t pc)
{
  int32_t b = *(int32_t*)pc;
  Insn_t i;
  
#define x( lo, len) ((b >> lo) & ((1 << len)-1))
#define xs(lo, len) (b << (32-lo-len) >> (32-len))
  
#include "decoder.h"

  //  fprintf(stderr, "Illegal instruction pc=%lx, %08x\n", pc, *(unsigned*)pc);
  i.op_code = Op_ILLEGAL;
  return i;
}
