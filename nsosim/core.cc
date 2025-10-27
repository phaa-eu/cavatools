/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "components.h"
#include "memory.h"
#include "core.h"

//thread_local long long cycle;
long long cycle;
long long mismatches;
  

bool Core_t::clock_pipeline() {
  mem_checker_used = false;
  Addr_t addr;
  bool dispatched = false;
  unsigned tmpflags = 0;
  ATTR_bv_t attr;

  Reason_t why_dispatch = Idle;
  Reason_t why_execute  = Idle;

  History_t* h = port.clock_port();
  if (h) {
    if (h->insn.rd() == NOREG) {	// stores retire immediately
      h->status = History_t::Retired;
      regs.value_is_ready(h->stbpos);
      regs.release_reg(h->stbpos);
    }
    else {
      if (regs.bus_busy(latency[h->insn.opcode()]))
	return 0;
      regs.reserve_bus(latency[h->insn.opcode()], h);
      h->status = History_t::Executing;
    }
    _inflight--;
  }

  // retire pipelined instruction
  h = regs.simulate_write_reg();
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
  why_dispatch = ready_to_dispatch(ir);
  if (why_dispatch != Ready)
    goto issue_from_queue;
  
  attr = attributes[ir.opcode()];

  // commit instruction for dispatch
  rename_input_regs(ir);
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

  // also integers
  //if ((attr==0) || (attr & (ATTR_uj|ATTR_cj|ATTR_amo|ATTR_ex))) {

  if ((attr & (ATTR_uj|ATTR_cj|ATTR_amo|ATTR_ex)) ||
      ir.opcode()==Op_addi   || ir.opcode()==Op_add ||
      ir.opcode()==Op_c_addi || ir.opcode()==Op_c_add) {
    
    //assert(ready_insn(ir));
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
    if (h->status != History_t::Queued_stbchk)
      h->status = History_t::Queued;
    why_execute = ready_to_execute(h);
    if (why_execute == Ready) {
      for (int j=k+1; j<last; ++j)
	queue[j-1] = queue[j];
      --last;
      goto execute_instruction;
    }
    else if (why_execute == Dependency_detected)
      goto finish_cycle;
  }
  // if here there was nothing to issue
  goto finish_cycle;		// no ready instruction in queue

 execute_instruction:
  // Note instruction is in ir but pc is in h->pc.

  ir = h->insn;
  if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) {
    assert(!port.full());
    Remapping_Regfile_t* rf = (ir.rd()==NOREG) ? 0 : &regs;
    port.request(mem_addr(ir), latency[ir.opcode()], h, rf);
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

#ifdef VERIFY
  if (ir.opcode() != Op_ecall)
    addr = perform(&ir, h->pc, h);
  else {
    s.reg[ir.rd()].a = h->expected_rd;
    addr = 0;
  }
#else
  addr = perform(&ir, h->pc, h);
#endif
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
  }
  if (! (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)))
    _inflight--;

 finish_cycle:
  not_dispatch[cycle % cycle_history] = why_dispatch;
  not_execute [cycle % cycle_history] = why_execute;
  ++cycle;
  
  // following code just for display, gets rewritten next iteration
  not_dispatch[cycle % cycle_history] = Idle;
  not_execute [cycle % cycle_history] = Idle;
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
    return (uintptr_t)((hart_t*)this)->s.frf[n-32].v[0];
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


static void null_simulator(class hart_t* h, Header_t* bb, uintptr_t* ap) { }

void Core_t::reset() {
  simulator = null_simulator;	// in hart_t

  regs.reset();

  memset(memory, 0, sizeof memory);
  last = 0;
  _inflight = 0;
  
  memset(&s.reg, 0, sizeof s.reg);
  memset(rob, 0, sizeof rob);
  memset(not_dispatch, 0, sizeof not_dispatch);
  memset(not_execute,  0, sizeof not_execute);

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















Reason_t Core_t::ready_to_execute(History_t* h)
{
  Insn_t ir = h->insn;
  ATTR_bv_t attr = attributes[ir.opcode()];

  if (!ready_insn(ir))
    return Regs_busy;

  if (ir.rd()!=NOREG && regs.bus_busy(latency[ir.opcode()]))
    return Bus_busy;

  if (! (attr & (ATTR_ld|ATTR_st)))
    return Ready;

  // memory operations have extra conditions
  if (port.full())
    return Port_busy;

  if (h->status != History_t::Queued_stbchk)
    return Ready;

  // loads need store buffer check
  if (mem_checker_used)
    return Stb_checker_busy;
  
  // special hack for internally substituted CAS instruction
  if (! (attr & ATTR_amo)) {
    ir.op.rs3 = check_store_buffer((s.reg[ir.rs1()].x+ir.immed())&~0x7L, h->stbpos);
    if (ir.rs3() != NOREG) {
      regs.acquire_reg(ir.rs3());
      h->insn = ir;
    }
  }
  h->status = History_t::Queued;
  return regs.busy(h->insn.rs3()) ? Dependency_detected : Ready;
}


Reason_t Core_t::ready_to_dispatch(Insn_t ir)
{
  ATTR_bv_t attr = attributes[ir.opcode()];
  if (regs.no_free_reg())
    return No_freereg;
  if (last == issue_queue_length)
    return IQ_full;
  
  rename_input_regs(ir);
  
  // branches must execute immediately
  if (attr & (ATTR_uj|ATTR_cj)) {
    if (!ready_insn(ir))
      return Br_regs_busy;
    if (ir.rd()!=NOREG && regs.bus_busy(latency[ir.opcode()]))
      return Br_bus_busy;
  }
  else if (attr & (ATTR_st | ATTR_amo|ATTR_ex)) {
    // stores need valid address and space in store buffer
    if (attr & ATTR_st) {
      if (regs.store_buffer_full())
	return Stb_full;
      if (regs.busy(ir.rs1()))
	return St_unknown_addr;
    }
    // AMO, ecall, other serializing instructions
    if (attr & (ATTR_amo|ATTR_ex)) {
      if (last > 0 || !port.empty())
	return Flush_wait;
      for (int r=0; r<max_phy_regs; ++r) {
	if (regs.busy(r))
	  return Flush_wait;
      }
    }
  }
  return Ready;
}
