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
//#include "perfctr.h"
#include "pipesim.h"


void trace_pipe(long next_report, long (*model_dcache)(long tr, const struct insn_t* p, long available))

#define TRACE
#include "mainloop.h"
