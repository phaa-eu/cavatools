#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"



thread_local long unsigned cycle;
thread_local membank_t memory[memory_channels][memory_banks];
thread_local membank_t memport;

void core_t::init_simulator() {
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
  last = 0;
  nextstb = 0;
  outstanding = 0;
  
  memset(&s.reg, 0, sizeof s.reg);

  // get started
  pc = get_state();
  bb = find_bb(pc);
  i = insnp(bb+1);

  rob[insns % dispatch_history] = { 0, pc, *i, cycle, History_t::STATUS_dispatch };
}

void core_t::simulate_cycle() {
  // retire memory operations
  for (int j=0; j<memory_channels; ++j) {
    for (int k=0; k<memory_banks; ++k) {
      membank_t* m = &memory[j][k];
      if (m->active() && m->finish==cycle) {
	busy[m->rd] = false;
	m->rd = NOREG;		  // indicate not active
      }
    }
  }

  // initiate new memory operation
  if (memport.active()) {
    membank_t* m = &memory[0][ (memport.addr>>3) % memory_banks ];
    if (! m->active()) {
      memport.finish += cycle;	// previously held latency
      *m = memport;
      memport.rd = NOREG;	// indicate is free
    }
  }
  
  // retire pipelined instruction(s)
  for (int k=0; k<num_write_ports; ++k) {
    History_t* h = wheel[k][index(0)];
    wheel[k][index(0)] = 0;
    if (h) {
      busy[h->insn.rd()] = false;	// destination register now available
      --outstanding;		// for serializing pipeline
      h->status = History_t::STATUS_retired;
    }
  }

  Insn_t ir = *i;
  ATTR_bv_t attr = attributes[ir.opcode()];
  History_t* h = &rob[insns % dispatch_history];
  uint16_t& flags = cycle_flags[cycle % cycle_history];
  uintptr_t addr;
  bool mem_checker_used = false;

  rename_input_regs(ir);
  
  // conditions that prevent dispatch into issue queue
  if (last == issue_queue_length) {
    flags |= FLAG_qfull;
    goto issue_from_queue;	// issue queue is full
  }
  if ((attr & (ATTR_uj|ATTR_cj)) && !ready_insn(ir)) {
    flags |= FLAG_busy;
    goto issue_from_queue;	// branch registers not ready
  }
  if ((attr & ATTR_st) && (busy[ir.rs1()] || stbuf_full())) {
    if (busy[ir.rs1()]) flags |= FLAG_stuaddr;
    if (stbuf_full())   flags |= FLAG_stbfull;
    goto issue_from_queue;	// unknown store address or SB full
  }
  if ((attr & ATTR_ex) && last>0) {
    flags |= FLAG_serialize;
    goto issue_from_queue;	// waiting for pipeline flush
  }

  // commit instruction for dispatch
  acquire_reg(ir.rs1());
  if (!ir.longimmed()) {
    acquire_reg(ir.rs2());
    acquire_reg(ir.rs3());
  }
  rename_output_reg(ir);

  if (attr & ATTR_st) {
    assert(!stbuf_full());
    // allocate store buffer entry
    addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;
    s.reg[stbuf(0)].a = addr;
    if (ir.rd() == NOREG) {	// use busy bit of store buffer "register"
      ir.op_rd = stbuf(0);
      acquire_reg(ir.rd());
      busy[ir.rd()];
    }
    nextstb = (nextstb+1) % store_buffer_length;

    // search for write-after-write dependency
    for (int k=1; k<store_buffer_length; ++k) {
      if (s.reg[stbuf(k)].a == addr) {        // add dependency
	// note this depends on all stores having short immediates
	ir.op.rs3 = stbuf(k);
	acquire_reg(ir.rs3());
	break;
      }
    }
    mem_checker_used = true; // prevent issuing loads
  }
  
  // enter into phantom ROB
  *h = { i, pc, ir, cycle, History_t::STATUS_queue };
  ++insns;
  ++outstanding;

  // advance pc sequentially, but taken branch will override
  pc += ir.compressed() ? 2 : 4;
  if (++i >= insnp(bb+1)+bb->count) {
    bb = find_bb(pc);
    i = insnp(bb+1);
  }

  // branches execute immediately
  if (attr & (ATTR_uj|ATTR_cj)) {
    assert(ready_insn(ir));
    goto execute_instruction;
  }

  // everything else goes into queue
  queue[last++] = h;

 issue_from_queue:

  // find first ready instruction
  h = 0;			// important!
  for (int k=0; k<last; ++k) {
    if (ready_insn(queue[k]->insn)) {
      h = queue[k];
      ir = h->insn;
      attr = attributes[ir.opcode()];
      // remove from queue
      for (int j=k+1; j<last; ++j)
	queue[j-1] = queue[j];
      --last;
      goto execute_instruction;
    }
  }
  // if here then there was no ready instruction

 execute_instruction:
  if (h) {  // instruction is in ir but pc is in h->pc
    
    // loads check store buffer for dependency
    if (attr & (ATTR_ld|ATTR_st)) {
      if (attr & ATTR_ld) {
	//mem_checker_used = true;
	for (int k=1; k<store_buffer_length; ++k) {
	  addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;
	  if (s.reg[stbuf(k)].a == addr) {
	    h->insn.op.rs3 = stbuf(k); // add dependency to this store
	    acquire_reg(stbuf(k));
	    break;
	  }
	}
      }
      memport.addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;
      memport.rd = ir.rd();
      memport.finish = latency[ir.opcode()];
    }

    // simulate reading register file
    release_reg(ir.rs1());
    if (! ir.longimmed()) {
      release_reg(ir.rs2());
      release_reg(ir.rs3());
    }
    
    // stores are actually "throws" because address already
    //   computed and available in store buffer
    if (is_store_buffer(ir.rd())) {
      busy[ir.rd()] = false;
      release_reg(ir.rd());
      h->status = History_t::STATUS_retired;
      --outstanding;
    }
    else {
      wheel[0][ index(latency[ir.opcode()]) ] = h;
      h->status = History_t::STATUS_execute;
    }

    // Tricky code here:  pc is actually associated with dispatched
    // instruction, whereas if we issued from queue ir and h->pc are
    // associated with instruction just dequeued.  Therefore if we
    // jumped it must have been a branch being dispatched this cycle.
    uintptr_t jumped = perform(&ir, h->pc);
    if (jumped) {
      pc = jumped;
      bb = find_bb(pc);
      i = insnp(bb+1);
    }
  }

 finish_cycle:
  ++cycle;
  cycle_flags[cycle % cycle_history] = 0;
  // following code just for display, gets rewritten next iteration
  ir = *i;
  rename_input_regs(ir);
  rob[insns % dispatch_history] = { i, pc, ir, cycle, History_t::STATUS_dispatch };
}

void core_t::rename_input_regs(Insn_t& ir)
{
  if (ir.rs1()!=NOREG) ir.op_rs1 = regmap[ir.rs1()];
  if (!ir.longimmed()) {
    if (ir.rs2()!=NOREG) ir.op.rs2 = regmap[ir.rs2()];
    if (ir.rs3()!=NOREG) ir.op.rs3 = regmap[ir.rs3()];
  }
}

void core_t::rename_output_reg(Insn_t& ir)
{
  uint8_t old_rd = ir.rd();
  if (old_rd!=0 && old_rd!=NOREG) {
    release_reg(regmap[old_rd]);
    ir.op_rd = freelist[--numfree];
    regmap[old_rd] = ir.rd();
    acquire_reg(ir.rd());
    busy[ir.rd()] = true;
  }
}

bool core_t::ready_insn(Insn_t ir)
{
  if (busy[ir.rs1()]) return false;
  if (!ir.longimmed()) {
    if (busy[ir.rs2()]) return false;
    if (busy[ir.rs3()]) return false;
  }
  return true;
}

void core_t::acquire_reg(uint8_t r)
{
  if (r==0 || r==NOREG)
    return;
  ++uses[r];
}

void core_t::release_reg(uint8_t r)
{
  if (r==0 || r==NOREG)
    return;
  assert(uses[r] > 0);
  if (--uses[r] == 0 && r < max_phy_regs)
    freelist[numfree++] = r;
}


uintptr_t core_t::get_state()
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

void core_t::put_state(uintptr_t pc)
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



long ooo_riscv_syscall(hart_t* h, long a0)
{
  core_t* c = (core_t*)h;
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
  core_t* parent = (core_t*)h;
  parent->put_state(parent->pc);
  core_t* child = new core_t(parent);
  return clone_thread(child);
}

void core_t::run_fast()
{
  for (;;) {
    uintptr_t jumped = perform(i, pc);
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
    ++insns;
    ++cycle;
  }
}
