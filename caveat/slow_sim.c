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



static inline int dump_regs( struct core_t* cpu, int n)
{
  for (int i=0; i<n; i++)
    fifo_put(&cpu->tb, regval[i]);
  return 0;
}

#define update_regfile(rd, val)  (withregs && (rd) != NOREG ? regval[updates++]=(val) : 0)
#define trace_mem(code, a)  fifo_put(&cpu->tb, trM(code, a))
#define trace_bbk(code, v)  ( fifo_put(&cpu->tb, trP(code, since, v)), restart() )
#define advance(sz)  { since+=sz; if (since >= tr_max_number-4L) { fifo_put(&cpu->tb, trP(tr_any, since, 0)); restart(); } }
#define restart()  (withregs ? dump_regs(cpu, updates) : 0, since=updates=0 )
//#define on_every_insn(p)  if (cpu->params.verify) { fifo_put(&verify, cpu->holding_pc); cpu->holding_pc=PC; }
#define on_every_insn(p)

#define amo_lock_begin
#define amo_lock_end


#define ICOUNT_INTERVAL 100000
  
void slow_sim( struct core_t* cpu, long total_max_count )
{
  Addr_t PC = cpu->pc;
  int since =0, updates=0;
  int withregs = (cpu->params.has_flags & tr_has_reg) != 0;
  
  while (cpu->state.mcause == 0 && total_max_count > 0) {
    long max_count = ICOUNT_INTERVAL;
    long countdown = max_count;

#include "sim_body.h"

    trace_mem(tr_icount, cpu->counter.insn_executed);
    total_max_count -= max_count;
  }
  if (since > 0) {
    trace_bbk(tr_any, 0);
  }
}
