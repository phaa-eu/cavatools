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
#define serialize()

#undef validate_csr
#define validate_csr(which, write) ({ \
  /* disallow writes to read-only CSRs */ \
  unsigned csr_read_only = get_field((which), 0xC00) == 3; \
  if ((write) && csr_read_only) \
    throw trap_illegal_instruction(insn.bits()); \
  /* other permissions checks occur in get_csr */ \
  (which); })

#define xlen 64

extern long (*golden[])(long pc, processor_t* p);


