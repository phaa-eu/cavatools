/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include "encoding.h"
#include "trap.h"
#include "arith.h"
#include "mmu.h"
#include "processor.h"

#undef set_pc_and_serialize
#define set_pc_and_serialize(x) STATE.pc = (x) & p->pc_alignment_mask()

#undef serialize
#define serialize(x)

#define xlen 64

extern long (*golden[])(long pc, processor_t* p);


