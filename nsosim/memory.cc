/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <cstdint>
#include <cassert>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "components.h"
#include "memory.h"
#include "core.h"

//thread_local Memory_t memory[memory_channels][memory_banks];
Memory_t memory[memory_channels][memory_banks];


void clock_memory_system()
{
  // retire memory operations
  for (int j=0; j<memory_channels; ++j) {
    for (int k=0; k<memory_banks; ++k) {
      Memory_t* m = &memory[j][k];
      if (m->active() && m->finish()==cycle)
	m->deactivate();
    }
  }
}




void Port_t::request(uintptr_t a, long long l, History_t* h, Remapping_Regfile_t* rf)
{
  assert(last < port_queue_length);
  port_req_t* p = &queue[last++];
  p->addr = a;
  p->latency = l;
  p->history = h;
  p->regfile = rf;
}

History_t* Port_t::clock_port()
{
  for (int k=0; k<last; ++k) {
    port_req_t* p = &queue[k];
    if (memory[mem_channel(p->addr)][mem_bank(p->addr)].active())
      continue;			// memory bank is busy
    if (p->regfile && p->regfile->bus_busy(p->latency))
      continue;			// register bus was busy
    memory[mem_channel(p->addr)][mem_bank(p->addr)].activate(cycle+p->latency, p->history);
    History_t* h = p->history;
    // remove from  queue
    for (int j=k+1; j<last; ++j)
      queue[j-1] = queue[j];
    --last;
    return h;
  }
  return 0;
} 
