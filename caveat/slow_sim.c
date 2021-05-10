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
#include "cache.h"
#include "perfctr.h"


struct cache_t icache;		/* instruction cache model */
struct cache_t dcache;		/* data cache model */


static Addr_t cur_line;		/* instruction cache optimization */
/*
  Trace record creation
*/
static uint64_t hart;		/* multiplexed hart 0..15 << 4 */
static long last_event;		/* for delta in trace record */

static inline void trMiss(enum tr_opcode code, Addr_t addr, long now)
{
  if (now-last_event > 0xff) {
    fifo_put(trace, (now<<8) | hart | tr_time);
    last_event=now;
  }
  fifo_put(trace, (addr<<16) | ((now-last_event)<<8) | hart | (long)code);
  last_event = now;
}

long load_latency, fma_latency, branch_delay;

#define mpy_cycles   8
#define div_cycles  32
#define fma_div_cycles (fma_latency*3)


#define amoB(cpu, name, r1, r2)
#define amoE(cpu, rd)

#define MEM_ACTION(a)  VA=a
#define JUMP_ACTION()  consumed=~0
#define STATS_ACTION() cpu->counter.cycles_simulated=now
#include "imacros.h"


void only_sim(struct core_t* cpu)
#include "sim_body.h"

#define COUNT
void count_sim(struct core_t* cpu)
#include "sim_body.h"
#undef COUNT

#define TRACE
void trace_sim(struct core_t* cpu)
#include "sim_body.h"
#undef TRACE

#define TRACE
#define COUNT
void count_trace_sim(struct core_t* cpu)
#include "sim_body.h"
#undef TRACE
#undef COUNT
  
