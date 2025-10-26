/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "memory.h"
#include "components.h"
#include "core.h"

void Remapping_Regfile_t::reset()
{
  memset(_busy, 0, sizeof _busy);
  memset(regmap, 0, sizeof regmap);
  // initialize register map and freelist
  for (int k=0; k<64; ++k) {
    regmap[k] = k;
    _uses[k] = 1;
  }
  numfree = 0;
  for (int k=64; k<max_phy_regs; ++k) {
    freelist[numfree++] = k;
    _uses[k] = 0;
  }
  stbtail = 0;
  memset(wheel, 0, sizeof wheel);
}


void Remapping_Regfile_t::reserve_bus(int latency, History_t* h)
{
  assert(! bus_busy(latency));
  wheel[index(latency)] = h;
}

void Remapping_Regfile_t::release_reg(uint8_t r)
{
  if (r == NOREG)
    return;
  if (--_uses[r] == 0) {
    _busy[r] = false;
    if (! is_store_buffer(r))
      freelist[numfree++] = r;
  }
}

Reg_t Remapping_Regfile_t::rename_reg(Reg_t arch_reg)
{
  if (arch_reg == NOREG)
    return NOREG;
  release_reg(regmap[arch_reg]);
  Reg_t r = freelist[--numfree];
  // update mapping table
  acquire_reg(r);
  regmap[arch_reg] = r;
  // now for instruction
  acquire_reg(r);
  _busy[r] = true;
  return r;
}

Reg_t Remapping_Regfile_t::allocate_store_buffer()
{
  assert(! store_buffer_full());
  Reg_t n = stbuf();
  stbtail = (stbtail+1) % store_buffer_length;
  acquire_reg(n);		// mark entry in use
  _busy[n] = true;
  return n;
}
