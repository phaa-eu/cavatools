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

#include "constants.h"


const char* reg_name[256] = {
  "zero","ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
  "s0",  "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
  "a6",  "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
  "s8",  "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6",
  "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
  "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
  "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
  "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
  "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
  "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
  "vm",
};
