/* Some Spike instruction semantics files include ../mmu.h
 */

#include "encoding.h"
#include "trap.h"
#include "arith.h"
#include "processor.h"
#include "internals.h"

#undef MMU
#undef set_pc
#undef set_pc_and_serialize
#undef serialize
#undef validate_csr

#define xlen 64

#define set_pc(x)				\
  do { p->check_pc_alignment(x);		\
    npc = MMU.jump_model(sext_xlen(x), pc);	\
  } while(0)

#define set_pc_and_serialize(x) STATE.pc = (x) & p->pc_alignment_mask()

#define serialize()

#define validate_csr(which, write) ({ \
  /* disallow writes to read-only CSRs */ \
  unsigned csr_read_only = get_field((which), 0xC00) == 3; \
  if ((write) && csr_read_only) \
    throw trap_illegal_instruction(insn.bits()); \
  /* other permissions checks occur in get_csr */ \
  (which); })

#if 0
#undef require
#undef require_extension
#undef require_align
#undef require_fp
#define require(x)
#define require_extension(x)
#define require_align(x,y)
#define require_fp
#endif

#include "mmu.h"

