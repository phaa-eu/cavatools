/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifndef NOSPIKE

#include "encoding.h"
#include "trap.h"
#include "arith.h"
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

extern long (*golden[])(long pc, class cpu_t* cpu);

#define NOISA

#ifdef NOISA
#undef require
#undef require_extension
#undef require_align
#undef require_fp
#define require(x)
#define require_extension(x)
#define require_align(x,y)
#define require_fp
#endif

#endif

#undef MMU

#define MMU (*cpu)

extern struct syscall_map_t rv_to_host[];
extern const int highest_ecall_num;
