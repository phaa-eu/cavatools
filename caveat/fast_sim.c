/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "caveat.h"
#include "caveat_fp.h"
#include "arith.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"


#define trace_mem(code, a)  0
#define trace_jmp(code, a)
#define trace_any(code, v)
#define advance(sz)
#define restart()
#define on_every_insn(p)

#define amo_lock_begin
#define amo_lock_end


void fast_sim( struct core_t* cpu, long max_count )
{
  assert(max_count > 0);
  register long countdown = max_count;
  register Addr_t PC = cpu->pc;

#include "sim_body.h"

}
