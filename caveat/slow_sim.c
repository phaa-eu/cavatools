/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
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

#define trace_mem(code, a)  fifo_put(&cpu->tb, trM(code, a))
#define trace_jmp(code, a)  ( fifo_put(&cpu->tb, trP(tr_jump, since, a)), restart() )
#define trace_any(code, v)  ( fifo_put(&cpu->tb, trP(code,  since, v)), restart() )
#define advance(sz)  { since+=sz; if (since >= tr_max_number-4L) { fifo_put(&cpu->tb, trP(tr_any, since, 0)); restart(); } }
#define restart()  since=0
#define on_every_insn(p)  if (cpu->params.verify) { fifo_put(&verify, cpu->holding_pc); cpu->holding_pc=PC; }

#define amo_lock_begin
#define amo_lock_end


#define ICOUNT_INTERVAL 1000

  
void slow_sim( struct core_t* cpu, long total_max_count )
{
  register Addr_t PC = cpu->pc;
  register long since = 0;
  
  while (cpu->state.mcause == 0 && total_max_count > 0) {
    register long max_count = ICOUNT_INTERVAL;
    register long countdown = max_count;

#include "sim_body.h"

    trace_mem(tr_icount, cpu->counter.insn_executed);
    total_max_count -= max_count;
  }
  if (since > 0)
    fifo_put(&cpu->tb, trP(tr_any, since, 0));
}
