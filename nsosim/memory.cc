/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <cstdint>
#include <cassert>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"
#include "memory.h"

thread_local Memory_t memory[memory_channels][memory_banks];
thread_local Memory_t port[memory_channels];


void clock_memory_system(Core_t* cpu)
{
  // retire memory operations
  for (int j=0; j<memory_channels; ++j) {
    for (int k=0; k<memory_banks; ++k) {
      Memory_t* m = &memory[j][k];
      if (m->active() && m->finish==cycle) {
	if (m->rd != NOREG) {
	  cpu->busy[m->rd] = false;
	  if (! is_store_buffer(m->rd))
	    cpu->release_reg(m->rd);
	}
	m->_active = false;
      }
    }
  }

  // initiate new memory operation from ports
  for (int j=0; j<memory_channels; ++j) {
    Memory_t* p = &port[j];
    if (p->active()) {
      Memory_t* m = &memory[j][mem_bank(p->addr)];
      if (! m->active()) {
	p->finish += cycle;	// previously held latency
	*m = *p;		// activate memory bank
	p->_active = 0;		// indicate is free
      }
    }
  }
}

Memory_t make_mem_descr(Addr_t a, long long f, Reg_t rd, short n) {
  Memory_t m;
  m.addr = a;
  m.finish = f;
  m.rd = rd;
  m._active = true;
  m.name = n;
  return m;
}

