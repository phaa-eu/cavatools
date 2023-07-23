
#if 0
/* Some Spike instruction semantics files include ../mmu.h
 */

#include "softfloat_types.h"
#include "softfloat.h"
#include "internals.h"
#include "specialize.h"

#include "mmu.h"
#include <assert.h>

#include "arith.h"
#include "encoding.h"
#include "decode.h"
#include "processor.h"

#define supports_extension(x)  1a

#undef require
#define require(x)

#undef require_extension
#define require_extension(x)

#undef require_fp
#define require_fp

#undef set_pc
#define set_pc(x)  npc=(sext_xlen(x))

#define xlen  64

#endif









/* Some Spike instruction semantics files include ../mmu.h
 */

#include "encoding.h"
//#include "trap.h"
#include "arith.h"
#include "processor.h"

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

#undef require
#undef require_extension
#undef require_align
#undef require_fp
#define require(x)
#define require_extension(x)
#define require_align(x,y)
#define require_fp

#undef RM
#define RM ({ int rm = insn.rm(); \
              if(rm == 7) rm = STATE.frm; \
              rm; })

#include "mmu.h"

