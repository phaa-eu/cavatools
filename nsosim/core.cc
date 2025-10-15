#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"



thread_local long unsigned cycle;	// count number of processor cycles
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

  rob[insns % history_depth] = { *i, pc, 0, cycle, 0 };
}

void core_t::simulate_cycle() {
  // retire memory operations
  for (int k=0; k<membank_number; ++k) {
    membank_t* m = &memory[k];
    if (m->rd && m->finish == cycle) {
#if 0
      if (is_store_buffer(m->rd)) // AMO are both loads and stores
	release_reg(m->rd);	  // wheel handles register release
#endif
      m->rd = 0;		  // indicate not active
    }
  }

  // initiate new memory operation
  if (memport.rd != 0) {
    membank_t* m = &memory[ (memport.addr>>3) % membank_number ];
    if (m->rd == 0) {
      *m = memport;
      memport.rd = 0;		// indicate is free
    }
  }
  
  // retire pipelined instruction(s)
  for (int k=0; k<num_write_ports; ++k) {
    History_t* h = wheel[k][index(0)];
    wheel[k][index(0)] = 0;
    if (h) {
      busy[h->insn.rd()] = false;	// destination register now available
      if (attributes[h->insn.opcode()] & ATTR_st) {
	s.reg[h->insn.rd()].a = 0;
	release_reg(h->insn.rd());
      }
      h->flags = 0;		// retired
      //h->ref = 0;		// do not show renaming anymore

      // decrement use count and free if zero
      release_reg(h->insn.rs1());
      if (!h->insn.longimmed()) {
	release_reg(h->insn.rs2());
	release_reg(h->insn.rs3());
      }
      --outstanding;		// for serializing pipeline
    }
  }

  Insn_t ir = *i;
  ATTR_bv_t attr = attributes[ir.opcode()];
  History_t* h = &rob[insns % history_depth];
  uintptr_t addr;
  bool mem_unit_used = false;

  rename_input_regs(ir);
  
  // conditions that prevent dispatch into issue queue
  if (last == issue_queue_length) {
    h->flags |= FLAG_qfull;
    goto issue_from_queue;	// issue queue is full
  }
  if ((attr & (ATTR_uj|ATTR_cj)) && !ready_insn(ir)) {
    h->flags |= FLAG_busy;
    goto issue_from_queue;	// branch registers not ready
  }
  if ((attr & ATTR_st) && (busy[ir.rs1()] || stbuf_full())) {
    h->flags |= FLAG_staddr;
    goto issue_from_queue;	// unknown store address or SB full
  }
  if ((attr & ATTR_ex) && last>0) {
    h->flags |= FLAG_serialize;
    goto issue_from_queue;	// waiting for pipeline flush
  }

  // commit instruction
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
      acquire_reg(stbuf(0));
    }
    nextstb = (nextstb+1) % store_buffer_length;
#if 0
    // search for write-after-write dependency
    for (int k=1; k<store_buffer_length; ++k) {
      if (s.reg[stbuf(k)].a == addr) {        // add dependency
	// note this depends on all stores having short immediates
	ir.op.rs3 = stbuf(k);
	acquire_reg(ir.rs3());
	break;
      }
    }
#endif
    mem_unit_used = true;	// prevent issuing loads
  }
  // enter into phantom ROB
  *h = { ir, pc, i, cycle, 0 };
  ++insns;
  ++outstanding;

  // put branches in front to execute immediately
  if (attr & (ATTR_uj|ATTR_cj)) { // pc will be handled then
    for (int k=last; k>=1; --k)	  // because may jump
      queue[k] = queue[k-1];
    queue[0] = h;		// front of queue
  }
  else { // everything else advances program counter
    pc += ir.compressed() ? 2 : 4;
    if (++i >= insnp(bb+1)+bb->count) {
      bb = find_bb(pc);
      i = insnp(bb+1);
    }
    queue[last] = h;		// end of queue
  }
  ++last;

 issue_from_queue:
  for (int k=0; k<last; ++k) {
    h = queue[k];
    ir = h->insn;
    attr = attributes[ir.opcode()];
    
    if (!ready_insn(ir))
      continue;
    
    // loads must check store buffer for dependency
    if (attr & ATTR_ld) {
      if (mem_unit_used)	// cannot enqueue store and execute
	continue;		// a load in same cycle
      for (int k=1; k<store_buffer_length; ++k) {
	addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;
	if (s.reg[stbuf(k)].a == addr) {
	  h->insn.op.rs3 = stbuf(k); // add dependency to this store
	  acquire_reg(stbuf(k));
	  h->flags |= FLAG_depend;
	  goto finish_cycle;	// cycle was "used up" by store buffer check
	  // note next time rs3 will not be ready
	}
      }
    }
    
    // loads and stores check memory bank not busy (must be last check)
    if (attr & (ATTR_ld|ATTR_st)) {
      if (memport.active())	// memory port is busy
	continue;
      memport.addr = (s.reg[ir.rs1()].x + ir.immed()) & ~0x7L;
      memport.rd = ir.rd();
      memport.finish = (cycle+1) + latency[ir.opcode()];
    }

    // found instruction for execution
    h->flags |= FLAG_execute;
    wheel[0][ index(latency[ir.opcode()]) ] = h;
    
    uintptr_t jumped = perform(&ir, h->pc);
    if (attr & (ATTR_uj|ATTR_cj)) { // pc will be handled then
      pc = jumped ? jumped : pc + (ir.compressed() ? 2 : 4);
      bb = find_bb(pc);
      i = insnp(bb+1);
    }
    else {
      pc += ir.compressed() ? 2 : 4;
      if (++i >= insnp(bb+1)+bb->count) {
	bb = find_bb(pc);
	i = insnp(bb+1);
      }
    }

    // remove from queue
    for (int j=k+1; j<last; ++j)
      queue[j-1] = queue[j];
    --last;
    goto finish_cycle;
  }

 finish_cycle:
  ++cycle;
  ir = *i;
  rename_input_regs(ir);
  rob[insns % history_depth] = { ir, pc, i, cycle, FLAG_decode };
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
