/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"
#include "memory.h"

thread_local long long cycle;
thread_local long long mismatches;


bool Core_t::clock_pipeline() {
  uint16_t& flags = cycle_flags[cycle % cycle_history];

  // Retire pipelined instruction(s)
  for (int k=0; k<num_write_ports; ++k) {
    History_t* h = wheel[k][index(0)];
    if (h) {
      Insn_t ir = h->insn;
      h->status = History_t::Retired;
#ifdef VERIFY
      if (h->actual_rd != h->expected_rd)
	++mismatches;
#endif
      busy[ir.rd()] = false;
      release_reg(ir.rd());
      if (h->lsqpos != NOREG) {
	busy[h->lsqpos] = false;
	release_reg(h->lsqpos);
      }
      --_inflight;
      wheel[k][index(0)] = 0;
    }
  }

  // Try to dispatch new instruction
  History_t* h = nextrob();
  Insn_t ir = *i;
  ATTR_bv_t attr = attributes[ir.opcode()];
  Addr_t addr;
  bool mem_checker_used = false;
  bool dispatched = false;

  rename_input_regs(ir);
  
  // conditions that prevent dispatch into issue queue
  if (last == issue_queue_length) {
    flags |= FLAG_qfull;
    goto issue_from_queue;	// issue queue is full
  }
  if ((attr & (ATTR_uj|ATTR_cj|ATTR_amo)) && !ready_insn(ir)) {
    flags |= FLAG_busy;
    goto issue_from_queue;	// branch registers not ready
  }
  if ((attr & ATTR_st) && busy[ir.rs1()]) {
    flags |= FLAG_stuaddr;
    goto issue_from_queue;	// unknown store address
  }
  if ((attr & (ATTR_ld|ATTR_st)) && lsq_full()) {
    flags |= FLAG_stbfull;
    goto issue_from_queue;	// lsq full
  }
  if ((attr & ATTR_ex) && last>0) {
    flags |= FLAG_serialize;
    goto issue_from_queue;	// waiting for pipeline flush
  }
  if ((attr & ATTR_amo) && (last>0 | port[0].active())) {
    flags |= FLAG_serialize;
    goto issue_from_queue;	// waiting for pipeline flush
  }

  // commit instruction for dispatch
  acquire_reg(ir.rs1());
  acquire_reg(ir.rs2());
  acquire_reg(ir.rs3());
  rename_output_reg(ir);
  h->status = History_t::Queued;

  if (attr & (ATTR_ld|ATTR_st)) {
    assert(!lsq_full());
    addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;

    // search for write-after-write dependency
    if (attr & ATTR_st) {
      // special hack for internally substituted CAS instruction
      if (! (attr & ATTR_amo))
	ir.op.rs3 = check_store_buffer(addr);
      h->lsqpos = allocate_lsq_entry(addr, (attr & ATTR_st)==true);
      mem_checker_used = true; // prevent issuing loads
    }
    else
      h->lsqpos = lsqtail + max_phy_regs;
    if (attr & ATTR_ld & !(attr & ATTR_amo))
      h->status = History_t::Queued_stbchk;
  }
  
  // enter into phantom ROB
  h->insn = ir;
  _insns++;
  _inflight++;
  dispatched = true;

  // advance pc sequentially, but taken branch will override
  pc += ir.compressed() ? 2 : 4;
  if (++i >= insnp(bb+1)+bb->count) {
    bb = find_bb(pc);
    i = insnp(bb+1);
  }

  // branches and AMO execute immediately
  if (attr & (ATTR_uj|ATTR_cj|ATTR_amo)) {
    assert(ready_insn(ir));
    goto execute_instruction;
  }

  // everything else goes into queue
  queue[last++] = h;

 issue_from_queue:

  // find first ready instruction
  for (int k=0; k<last; ++k) {
    h = queue[k];
    ir = h->insn;
    attr = attributes[ir.opcode()];
    if (ready_insn(ir)) {
      
      // memory operations have extra conditions
      if (attr & (ATTR_ld|ATTR_st)) {
	int channel = 0;
	if (port[channel].active())
	  continue;

	if (h->status == History_t::Queued_stbchk) {
	  if (mem_checker_used)
	    continue;
	  h->status = History_t::Queued;

	  // special hack for internally substituted CAS instruction
	  if (! (attr & ATTR_amo))
	    ir.op.rs3 = check_store_buffer((s.reg[ir.rs1()].x+ir.immed())&~0x7L, h->lsqpos);
	  
	  if (ir.rs3() != NOREG) {
	    acquire_reg(ir.rs3());
	    flags |= FLAG_stbhit;
	    h->insn = ir;
	    if (busy[h->insn.rs3()]) // store has not retired so we must wait
	      goto finish_cycle; // cycle "used up" by store buffer check
	  }
	}
      }
      
      // remove from queue
      for (int j=k+1; j<last; ++j)
	queue[j-1] = queue[j];
      --last;
      goto execute_instruction;
    }
  }
  goto finish_cycle;		// no ready instruction in queue

 execute_instruction:
  // Note instruction is in ir but pc is in h->pc.

  if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) {
    int channel = 0;
    assert(!port[channel].active());
    port[channel] = make_mem_descr(s.reg[h->lsqpos].a, latency[ir.opcode()], h);
  }
    
  // simulate reading register file
  release_reg(ir.rs1());
  release_reg(ir.rs2());
  release_reg(ir.rs3());
  
  // Tricky code here:  pc is actually associated with dispatched
  // instruction, whereas if we issued from queue ir and h->pc are
  // associated with instruction just dequeued.  Therefore if we
  // jumped it must have been a branch being dispatched this cycle.
    
  addr = perform(&ir, h->pc, h);
  if (addr) {		// if no taken branch pc already incremented
    pc = addr;
    bb = find_bb(pc);
    i = insnp(bb+1);
  }
#ifdef VERIFY
  h->actual_rd = ir.rd()!=NOREG ? s.reg[ir.rd()].a : 0;
#endif

  if (!(attr & (ATTR_ld|ATTR_st))) 
    wheel[0][ index(latency[ir.opcode()]) ] = h;
  h->status = History_t::Executing;

 finish_cycle:
  ++cycle;
  cycle_flags[cycle % cycle_history] = 0;
  
  // following code just for display, gets rewritten next iteration
  ir = *i;
  rename_input_regs(ir);

  // prepare next history
  h = nextrob();
  h->clock = cycle;
  h->insn = ir;
  h->pc = pc;
  h->ref = *i;
  h->status = History_t::Dispatch;
  h->lsqpos = NOREG;

  return dispatched;		// was instruction dispatched?
}

void Core_t::rename_input_regs(Insn_t& ir)
{
  if (ir.rs1()!=NOREG) ir.op_rs1 = regmap[ir.rs1()];
  if (ir.rs2()!=NOREG) ir.op.rs2 = regmap[ir.rs2()];
  if (ir.rs3()!=NOREG) ir.op.rs3 = regmap[ir.rs3()];
}

void Core_t::rename_output_reg(Insn_t& ir)
{
  uint8_t old_rd = ir.rd();
  if (old_rd!=0 && old_rd!=NOREG) {
    release_reg(regmap[old_rd]);
    ir.op_rd = freelist[--numfree];
    regmap[old_rd] = ir.rd();
    acquire_reg(ir.rd());
    // now model getting register for in flight
    acquire_reg(ir.rd());
    busy[ir.rd()] = true;
  }
}

bool Core_t::ready_insn(Insn_t ir)
{
  if (busy[ir.rs1()]) return false;
  if (!ir.longimmed()) {
    if (busy[ir.rs2()]) return false;
    if (busy[ir.rs3()]) return false;
  }
  return true;
}

void Core_t::acquire_reg(uint8_t r)
{
  if (r==0 || r==NOREG)
    return;
  ++uses[r];
}

void Core_t::release_reg(uint8_t r)
{
  if (r==0 || r==NOREG)
    return;
  assert(uses[r] > 0);
  if (--uses[r] == 0) {
    if (is_store_buffer(r))
      s.reg[r].a = 0;		// mark as free
    else
      freelist[numfree++] = r;
  }
}

bool Core_t::lsq_active(Reg_t r)
{
  return busy[r] || uses[r] > 0 || s.reg[r].a != 0;
}

bool Core_t::lsq_full()
{
  return lsq_active(lsqtail + max_phy_regs);
}

Reg_t Core_t::allocate_lsq_entry(uintptr_t addr, bool is_store)
{
  Reg_t n = lsqtail + max_phy_regs;
  lsqtail = (lsqtail+1) % lsq_length;
  assert(is_store_buffer(n));
  s.reg[n].a = addr;		// mark in use
  busy[n] = is_store;
  acquire_reg(n);
  return n;
}

Reg_t Core_t::check_store_buffer(uintptr_t addr, Reg_t k)
{
  if (k == NOREG)
    k = lsqtail + max_phy_regs;
  while ((k=(k-1+lsq_length)%lsq_length) != lsqtail) {
    Reg_t r = k + max_phy_regs;
    if (busy[r] && s.reg[r].a == addr) {
      acquire_reg(r);
      return r;
    }
  }
  return NOREG;
}


uintptr_t Core_t::get_state()
{
  hart_t* h = this;
  for (int k=1; k<32; k++)
    s.reg[regmap[k]].x = h->s.xrf[k];
  for (int k=0; k<32; k++)
    s.reg[regmap[k+32]].f = h->s.frf[k];
  s.fflags = h->s.fflags;
  s.frm = h->s.frm;
  return h->pc;
}

void Core_t::put_state(uintptr_t pc)
{
  hart_t* h = this;
  for (int k=0; k<32; k++)
    h->s.xrf[k] = s.reg[regmap[k]].x;
  for (int k=0; k<32; k++)
    h->s.frf[k] = s.reg[regmap[k+32]].f;
  h->s.fflags = s.fflags;
  h->s.frm = s.frm;
  h->pc = pc;
}

uintptr_t Core_t::get_rd_from_spike(Reg_t n) {
  if (n == NOREG)
    return 0;
  else if (n < 32)
    return ((hart_t*)this)->s.xrf[n];
  else
    return (uintptr_t)((hart_t*)this)->s.frf[n].v[0];
}



long ooo_riscv_syscall(hart_t* h, long a0)
{
  Core_t* c = (Core_t*)h;
  long a1 = c->s.reg[c->regmap[11]].x;
  long a2 = c->s.reg[c->regmap[12]].x;
  long a3 = c->s.reg[c->regmap[13]].x;
  long a4 = c->s.reg[c->regmap[14]].x;
  long a5 = c->s.reg[c->regmap[15]].x;
  long rvnum = c->s.reg[c->regmap[17]].x;
  long rv = proxy_syscall(rvnum, a0, a1, a2, a3, a4, a5, c);
  return rv;
}

int clone_proxy(hart_t* h)
{
  Core_t* parent = (Core_t*)h;
  parent->put_state(parent->pc);
  Core_t* child = new Core_t(parent);
  return clone_thread(child);
}


void Core_t::test_run()
{
  while (1) {
    uintptr_t jumped = perform(i, pc, &rob[0]);
    if (jumped) {
      pc = jumped;
      bb = find_bb(pc);
      i = insnp(bb+1);
    }
    else {
      pc += i->compressed() ? 2 : 4;
      if (++i >= insnp(bb+1)+bb->count) {
	bb = find_bb(pc);
	i = insnp(bb+1);
      }
    }
    ++cycle;
    ++_insns;
  }
}



static void null_simulator(class hart_t* h, Header_t* bb, uintptr_t* ap) { }

void Core_t::reset() {
  simulator = null_simulator;	// in hart_t
  
  memset(busy, 0, sizeof busy);
  memset(regmap, 0, sizeof regmap);
  // initialize register map and freelist
  for (int k=0; k<64; ++k) {
    regmap[k] = k;
    uses[k] = 1;
  }
  numfree = 0;
  for (int k=64; k<max_phy_regs; ++k) {
    freelist[numfree++] = k;
    uses[k] = 0;
  }
  regmap[NOREG] = NOREG;
  uses[NOREG] = 0;
  // initialize pipelines and issue queue
  memset(wheel, 0, sizeof wheel);
  memset(memory, 0, sizeof memory);
  last = 0;
  lsqtail = 0;
  _inflight = 0;
  
  memset(&s.reg, 0, sizeof s.reg);
  memset(rob, 0, sizeof rob);
  memset(cycle_flags, 0, sizeof cycle_flags);

  // get started
  pc = get_state();
  bb = find_bb(pc);
  i = insnp(bb+1);

  History_t* h = nextrob();
  h->clock = cycle;
  h->insn = *i;
  h->pc = pc;
  h->ref = *i;
  h->status = History_t::Dispatch;
  h->lsqpos = NOREG;
}


#if 0
// leftover stuff

History_t make_history(long long c, Insn_t ir, Addr_t p, Insn_t* i, History_t::Status_t s) {
  History_t h;
  h.clock = c;
  h.insn = ir;
  h.pc = p;
  h.ref = i;
  h.status = s;
  h.lsqpos = NOREG;
  return h;
}

#endif

