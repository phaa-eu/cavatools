/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <cstdint>
#include <cassert>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

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

