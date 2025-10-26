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

//thread_local long long cycle;
long long cycle;
long long mismatches;


void Core_t::clock_port() {
    // Launch new memory operation if:
  //   valid request in port
  //   register write bus avail (if needed)
  //   memory bank is idle
  if (!port.active())
    return;			// idle
  History_t* h = port.history();
  
  if (memory[mem_channel(port.addr())][mem_bank(port.addr())].active())
    return;			// memory bank is busy

  memory[mem_channel(port.addr())][mem_bank(port.addr())].activate(cycle+port.latency(), h);
  if (h->insn.rd() == NOREG) {	// stores retire immediately
    h->status = History_t::Retired;
    regs.value_is_ready(h->stbpos);
    regs.release_reg(h->stbpos);
  }
  else {
    if (regs.bus_busy(port.latency()))
      return;
    regs.reserve_bus(port.latency(), h);
    h->status = History_t::Executing;
  }
  port.deactivate();
}
  

bool Core_t::clock_pipeline() {
  uint16_t& flags = cycle_flags[cycle % cycle_history];

  clock_port();

  // retire pipelined instruction
  History_t* h = regs.simulate_write_reg();
  if (h) {
    regs.value_is_ready(h->insn.rd());
    regs.release_reg(h->insn.rd());
    h->status = History_t::Retired;
    if (attributes[h->insn.opcode()] & ATTR_st) {
      regs.value_is_ready(h->stbpos);
      regs.release_reg(h->stbpos);
    }
#ifdef VERIFY
    if (h->actual_rd != h->expected_rd || h->pc != h->expected_pc)
      ++mismatches;
#endif
  }  

  // Try to dispatch new instruction
  h = nextrob();
  Insn_t ir = *i;
  ATTR_bv_t attr = attributes[ir.opcode()];
  Addr_t addr;
  bool mem_checker_used = false;
  bool dispatched = false;
  unsigned tmpflags = 0;

  // conditions that prevent dispatch into issue queue

  if (regs.no_free_reg()) {
    flags |= FLAG_nofree;
    goto issue_from_queue;
  }
  if (last == issue_queue_length) {
    flags |= FLAG_qfull;
    goto issue_from_queue;	// issue queue is full
  }
  
  rename_input_regs(ir);

  // branches execute immediately
  if (attr & (ATTR_uj|ATTR_cj)) {
    if (!ready_insn(ir)) {
      flags |= FLAG_busy;
      goto issue_from_queue;	// branch registers not ready
    }
    if (ir.rd()!=NOREG && regs.bus_busy(latency[ir.opcode()])) {
      flags |= FLAG_regbus;
      goto issue_from_queue;	// branch registers not ready
    }
  }
  else if (attr & (ATTR_st | ATTR_amo|ATTR_ex)) {
    // stores need valid address and space in store buffer
    if (attr & ATTR_st) {
      if (regs.store_buffer_full()) {
	flags |= FLAG_stbfull;
	goto issue_from_queue;	// lsq full
      }
      if (regs.busy(ir.rs1())) {
	flags |= FLAG_stuaddr;
	goto issue_from_queue;	// unknown store address
      }
    }
    // atomic memory operations, ecalls and CSR instructions
    if (attr & (ATTR_amo|ATTR_ex)) {
      flags |= FLAG_serialize;
      if (last > 0)		// issue all deferred instructions
	goto issue_from_queue;
      if (port.active())	// including pending memory operations
	goto issue_from_queue;
      for (int r=0; r<max_phy_regs; ++r) {
	if (regs.busy(r))
	  goto issue_from_queue; // and flush pipelines
      }
    }
  }

  // commit instruction for dispatch
  regs.acquire_reg(ir.rs1());
  if (!ir.longimmed()) {
    regs.acquire_reg(ir.rs2());
    regs.acquire_reg(ir.rs3());
  }
  ir.op_rd = regs.rename_reg(ir.rd());
  h->status = History_t::Queued;

  if (attr & (ATTR_ld|ATTR_st)) {
    addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;

    // search for write-after-write dependency
    if (attr & ATTR_st) {
      assert(!regs.store_buffer_full());
      // special hack for internally substituted CAS instruction
      if (! (attr & ATTR_amo)) 
	ir.op.rs3 = check_store_buffer(addr);
      h->stbpos = regs.allocate_store_buffer();
      s.reg[h->stbpos].a = addr;
      mem_checker_used = true; // prevent issuing loads
    }
    else
      h->stbpos = regs.stbuf();
    if ((attr & ATTR_ld) & !(attr & ATTR_amo))
      h->status = History_t::Queued_stbchk;
  }
  h->insn = ir;

  // advance pc sequentially, but taken branch will override
  pc += ir.compressed() ? 2 : 4;
  if (++i >= insnp(bb+1)+bb->count) {
    bb = find_bb(pc);
    i = insnp(bb+1);
  }

  // branches, AMO execute immediately
  if (attr & (ATTR_uj|ATTR_cj|ATTR_amo|ATTR_ex)) {
    assert(ready_insn(ir));
    // put at front of issue queue
    for (int k=last; k>0; --k)
      queue[k] = queue[k-1];
    queue[0] = h;
    ++last;
  }
  else // everything else goes in back of queue
    queue[last++] = h;
  
  // enter into phantom ROB
  dispatched = true;
  _inflight++;
  _insns++;
  memset(&rob[insns() % dispatch_history], 0, sizeof(History_t));

 issue_from_queue:

  // find first ready instruction
  for (int k=0; k<last; ++k) {
    h = queue[k];
    
    if (h->status == History_t::Queued_noport || h->status == History_t::Queued_nochk)
      h->status = History_t::Queued;
      
    ir = h->insn;
    attr = attributes[ir.opcode()];
    if (ready_insn(ir)) {
      // check register file write bus
      if (ir.rd() != NOREG) {
	if (regs.bus_busy(latency[ir.opcode()])) {
	  tmpflags |= FLAG_regbus;
	  continue;
	}
      }
      // memory operations have extra conditions
      if (attr & (ATTR_ld|ATTR_st)) {
	// memory operation cannot issue if port is busy
	if (port.active()) {
	  tmpflags |= FLAG_noport;
	  h->status = History_t::Queued_noport;
	  continue;
	}
	if (h->status == History_t::Queued_stbchk) {
	  if (mem_checker_used) {
	    tmpflags |= FLAG_stbchk;
	    continue;
	  }
	  h->status = History_t::Queued;

	  // special hack for internally substituted CAS instruction
	  if (! (attr & ATTR_amo)) {
	    ir.op.rs3 = check_store_buffer((s.reg[ir.rs1()].x+ir.immed())&~0x7L, h->stbpos);
	    if (ir.rs3() != NOREG) {
	      regs.acquire_reg(ir.rs3());
	      flags |= FLAG_stbhit;
	      h->insn = ir;
	      if (regs.busy(h->insn.rs3())) // store not retired
		goto finish_cycle;	    // cycle "used up"
	    }
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
  flags |= tmpflags;		// reasons could not issue
  goto finish_cycle;		// no ready instruction in queue

 execute_instruction:
  // Note instruction is in ir but pc is in h->pc.

  ir = h->insn;
  if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) {
    assert(!port.active());
    port.request(mem_addr(ir), latency[ir.opcode()], h);
    //    if (attributes[ir.opcode()] & ATTR_st)
    //      regs.release_reg(h->stbpos);
  }
    
  // simulate reading register file
  regs.release_reg(ir.rs1());
  if (!ir.longimmed()) {
    regs.release_reg(ir.rs2());
    regs.release_reg(ir.rs3());
  }
  
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
  if (attributes[ir.opcode()] & ATTR_ld)
    h->actual_rd = h->expected_rd;
  else if (attributes[ir.opcode()] & ATTR_st)
    h->actual_rd = s.reg[ir.rs2()].a;
  else
    h->actual_rd = ir.rd()!=NOREG ? s.reg[ir.rd()].a : 0;
#endif
  
  if (ir.rd()==NOREG && !(attributes[ir.opcode()] & ATTR_st)) {
    h->status = History_t::Retired; // branches, fence...
  }
  else {
    if (ir.rd()!=NOREG && !(attributes[ir.opcode()] & ATTR_ld))
      regs.reserve_bus(latency[ir.opcode()], h);
    h->status = History_t::Executing;
    _inflight--;
  }

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
  h->stbpos = NOREG;

  return dispatched;		// was instruction dispatched?
}

void Core_t::rename_input_regs(Insn_t& ir)
{
  if (ir.rs1() != NOREG) ir.op_rs1 = regs.map(ir.rs1());
  if (! ir.longimmed()) {
    if (ir.rs2() != NOREG) ir.op.rs2 = regs.map(ir.rs2());
    if (ir.rs3() != NOREG) ir.op.rs3 = regs.map(ir.rs3());
  }
}

bool Core_t::ready_insn(Insn_t ir)
{
  if (regs.busy(ir.rs1())) return false;
  if (!ir.longimmed()) {
    if (regs.busy(ir.rs2())) return false;
    if (regs.busy(ir.rs3())) return false;
  }
  return true;
}


Reg_t Core_t::check_store_buffer(uintptr_t addr, int k)
{
  for (int k=1; k<store_buffer_length; ++k) {
    Reg_t r = regs.stbuf(k);
    if (regs.busy(r) && s.reg[r].a == addr) {
      regs.acquire_reg(r);	// create dependency
      return r;
    }
  }
  return NOREG;
}

uintptr_t Core_t::get_state()
{
  hart_t* h = this;
  for (int k=0; k<32; k++)
    s.reg[regs.map(k)].x = h->s.xrf[k];
  for (int k=0; k<32; k++)
    s.reg[regs.map(k+32)].f = h->s.frf[k];
  s.fflags = h->s.fflags;
  s.frm = h->s.frm;
  return h->pc;
}

void Core_t::put_state(uintptr_t pc)
{
  hart_t* h = this;
  for (int k=0; k<32; k++)
    h->s.xrf[k] = s.reg[regs.map(k)].x;
  for (int k=0; k<32; k++)
    h->s.frf[k] = s.reg[regs.map(k+32)].f;
  h->s.fflags = s.fflags;
  h->s.frm = s.frm;
  h->pc = pc;
}

uintptr_t Core_t::get_rd_from_spike(Reg_t n) {
  //if (n == NOREG)
  //  return 0;
  //else if (n < 32)
  if (n < 32)
    return ((hart_t*)this)->s.xrf[n];
  else
    return (uintptr_t)((hart_t*)this)->s.frf[n].v[0];
}

uintptr_t Core_t::get_pc_from_spike() {
  return ((hart_t*)this)->pc;
}



long ooo_riscv_syscall(hart_t* h, long a0)
{
  Core_t* c = (Core_t*)h;
  long a1 = c->s.reg[c->regs.map(11)].x;
  long a2 = c->s.reg[c->regs.map(12)].x;
  long a3 = c->s.reg[c->regs.map(13)].x;
  long a4 = c->s.reg[c->regs.map(14)].x;
  long a5 = c->s.reg[c->regs.map(15)].x;
  long rvnum = c->s.reg[c->regs.map(17)].x;
  long rv = proxy_syscall(rvnum, a0, a1, a2, a3, a4, a5, h);
  return rv;
}

int clone_proxy(hart_t* h)
{
  fprintf(stderr, "NOOOO clone_proxy()\n");
  abort();
  
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

  regs.reset();

  memset(memory, 0, sizeof memory);
  last = 0;
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
  h->stbpos = NOREG;
}



