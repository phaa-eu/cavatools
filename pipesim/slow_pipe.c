/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "cache.h"
#include "perfctr.h"
#include "pipesim.h"


long dcache_writethru(long tr, const struct insn_t* p, long available)
{
  long addr = tr_value(tr);
  long tag = addr >> dc.lg_line;
  long when = 0;
  if (writeOp(p->op_code)) {
    long sz = tr_size(tr);
    if (sz < 8) {	/* < 8B need L1 for ECC, 8B do not allocate */
      when = lookup_cache(&dc, addr, 0, available);
      if (when == available)
	fifo_put(out, trM(tr_d1get, addr));
    }
    fifo_put(out, tr);
  }
  else
    when = lookup_cache(&dc, addr, 0, available);
  if (when == available) { /* cache miss */
    fifo_put(out, trM(tr_d1get, addr));
  }
  return when;
}


long dcache_writeback(long tr, const struct insn_t* p, long available)
{
  long addr = tr_value(tr);
  long tag = addr >> dc.lg_line;
  long when = lookup_cache(&dc, addr, writeOp(p->op_code), available);
  if (when == available) { /* cache miss */
    if (*dc.evicted)
      fifo_put(out, trM(tr_d1put, *dc.evicted<<dc.lg_line));
    fifo_put(out, trM(tr_d1get, addr));
  }
  return when;
}


void slow_pipe(long next_report, long (*model_dcache)(long tr, const struct insn_t* p, long available))

#define SLOW
#include "mainloop.h"
