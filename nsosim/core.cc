#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"



thread_local long unsigned cycle;	// count number of processor cycles

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
      if (is_store_buffer(m->rd)) // AMO are both loads and stores
	release_reg(m->rd);	  // wheel handles register release
      m->rd = 0;
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
  unsigned flags = 0;
  int idx;
  rename_input_regs(ir);
  
  // detect special cases that cannot go into queue
  if (attr & (ATTR_uj|ATTR_cj|ATTR_st|ATTR_ex)) {
    if ((attr & (ATTR_uj|ATTR_cj)) && !ready_insn(ir)  ||
	(attr &  ATTR_ex         ) && outstanding > 0  ||
	(attr &  ATTR_st         ) && busy[ir.rs1()])  {
      //rob[insns % history_depth].flags = flags | FLAG_decode;
      goto issue_from_queue;
    }
    if (attr & ATTR_st) {
      // check if room in store queue
      if (s.reg[stbuf(0)].a != 0) {
	rob[insns % history_depth].flags |= FLAG_stbuf;
	goto issue_from_queue;
      }
      // allocate store buffer entry
      ir.op_rd = stbuf(0);
      nextstb = (nextstb+1) % store_buffer_length;
      // check store queue for hits
      uintptr_t addr = s.reg[ir.rs1()].x + ir.immed();
      assert(addr != 0);
      for (int k=1; k<store_buffer_length; ++k) {
	if (s.reg[stbuf(k)].a == addr) {	// add dependency to this store
	  // note this depends on all stores having short immediates
	  ir.op.rs3 = stbuf(k);
	  acquire_reg(stbuf(k));
	  break;
	}
      }
    }
  }

  if (outstanding > 0) {
    if (issue_from_queue())
      goto finish;
  }

  if (can_execute_insn(ir, pc)) {
    commit_insn(ir);
    uintptr_t jumped = perform(&ir, pc);
    flags |= FLAG_execute;
    History_t* h = &rob[insns++ % history_depth];
    *h = { ir, pc, i, cycle, flags };
    
    // advance pc
    if (jumped) {
      pc = jumped;
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
    goto finish;
  }

  // otherwise try to enqueue instruction
  flags |= FLAG_busy;
  if (last == issue_queue_length) {
    flags |= FLAG_qfull;
  }
  else {
    commit_insn(ir);
    History_t* h = &rob[insns++ % history_depth];
    *h = { ir, pc, i, cycle, flags };
    queue[last++] = h;
    // advance pc, can never be branch
    pc += ir.compressed() ? 2 : 4;
    if (++i >= insnp(bb+1)+bb->count) {
      bb = find_bb(pc);
      i = insnp(bb+1);
    }
  }

 issue_from_queue:
  issue_from_queue();

 finish:
  ++cycle;
  ir = *i;
  rename_input_regs(ir);
  rob[insns % history_depth] = { ir, pc, i, cycle, FLAG_decode };
}

bool core_t::can_execute_insn(Insn_t ir, uintptr_t pc)
{
  // check output bus available
  int idx = index(latency[ir.opcode()]);
  if (wheel[0][idx])
    return false;

  // check memory bank not busy (must be last check)
  if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) {
    int bank = ((s.reg[ir.rs1()].x + ir.immed()) >> 3) % membank_number;
    membank_t* m = &memory[bank];
    if (m->rd)
      return false;
    m->rd = ir.rd();
    m->finish = cycle+latency[ir.opcode()];
  }

  // stores handled by memory bank, not wheel
  // but AMO uses both
  if (! is_store_buffer(ir.rd()))
    wheel[0][ index(latency[ir.opcode()]) ] = &rob[insns % history_depth];

  return true;
}

bool core_t::issue_from_queue()
{
  unsigned flags = 0;
  for (int k=0; k<last; ++k) {
    History_t* h = queue[k];
    Insn_t ir = h->insn;
    if (ready_insn(ir)) {
      // special cases
      if (attributes[ir.opcode()] & ATTR_ld) {
	uintptr_t addr = s.reg[ir.rs1()].x + ir.immed();
	assert(addr != 0);
	// check store buffer hits
	for (int k=1; k<store_buffer_length; ++k) {
	  if (s.reg[stbuf(k)].a == addr) { // add dependency to this store
	    h->insn.op.rs3 = stbuf(k);
	    acquire_reg(stbuf(k));
	    h->flags |= FLAG_depend;
	    return true;	// this issue cycle was "used up"
	  }
	}
      }

      // general case
      if (can_execute_insn(ir, h->pc)) {
	(void)perform(&ir, h->pc); // no branches in queue
	h->flags |= FLAG_execute;
	// remove from issue queue
	for (int j=k+1; j<last; ++j)
	  queue[j-1] = queue[j]; // later instructions move up
	--last;			 // one slot
	return true;
      }
    }
  }
  return false;
}


void core_t::rename_input_regs(Insn_t& ir)
{
  ir.op_rs1 = regmap[ir.rs1()];
  if (!ir.longimmed()) {
    ir.op.rs2 = regmap[ir.rs2()];
    ir.op.rs3 = regmap[ir.rs3()];
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

void core_t::commit_insn(Insn_t& ir)
{
  acquire_reg(ir.rs1());
  if (!ir.longimmed()) {
    acquire_reg(ir.rs2());
    acquire_reg(ir.rs3());
  }
  ++outstanding;
  if (ir.rd() == NOREG || ir.rd() == 0)
    return;
  // rename output register if not store buffer
  if (ir.rd() < 64) {
      uint8_t old_rd = ir.rd();
      release_reg(regmap[old_rd]);
      ir.op_rd = freelist[--numfree];
      regmap[old_rd] = ir.rd();
  }
  acquire_reg(ir.rd());
  busy[ir.rd()] = true;
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
  if (--uses[r] == 0) {
    if (r < max_phy_regs)
      freelist[numfree++] = r;
    else
      s.reg[r].a = 0;		// free store buffer entry
  }
}



uintptr_t core_t::get_state()
{
  hart_t* h = this;
  memset(&s, 0, sizeof s);
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
