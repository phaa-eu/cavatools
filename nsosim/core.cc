#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"





uintptr_t perform(Insn_t* i, uintptr_t pc);

void core_t::init_simulator() {
  memset(busy, 0, sizeof busy);
  // initialize register map and freelist
  for (int k=0; k<64; ++k) {
    regmap[k] = k;
    uses[k] = 1;
  }
  numfree = 0;
  for (int k=64; k<64+max_phy_regs; ++k) {
    freelist[numfree++] = k;
    uses[k] = 0;
  }
  regmap[NOREG] = NOREG;
  uses[NOREG] = 0;
  // initialize pipelines and issue queue
  memset(wheel, 0, sizeof wheel);
  last = 0;
  outstanding = 0;

  // get started
  pc = get_state();
  bb = find_bb(pc);
  i = insnp(bb+1);

  rob[insns % history_depth] = { *i, pc, 0, cycle, 0 };
}

void core_t::simulate_cycle() {
  // retire pipelined instruction(s)
  for (int k=0; k<num_write_ports; ++k) {
    History_t* h = wheel[k][index(0)];
    if (h) {
      Insn_t* p = &h->insn;
      busy[p->rd()] = false;	// destination register available
      wheel[k][index(0)]->flags = 0;
      wheel[k][index(0)]->ref = 0;
      // decrement use count and free if zero
      release_reg(p->rs1());
      if (!p->longimmed()) {
	release_reg(p->rs2());
	release_reg(p->rs3());
      }
      --outstanding;
      wheel[k][index(0)] = 0;
    }
  }

  // detect special cases
  Insn_t ir = *i;
  unsigned flags = 0;
  ATTR_bv_t attr = attributes[ir.opcode()];
  if (attr & (ATTR_uj|ATTR_cj|ATTR_st|ATTR_ex)) {
    if (attr & (ATTR_uj|ATTR_cj))	flags |= FLAG_jump;
    if (attr &  ATTR_st)		flags |= FLAG_store;
    if (attr &  ATTR_ex)		flags |= FLAG_serialize;
  }
    
  rename_input_regs(ir);
  if (flags || no_free_reg(ir)) {
    if ((flags & FLAG_serialize) && outstanding > 0 ||
	(flags & FLAG_store)     && busy[ir.rs1()]  ||
	(flags & FLAG_jump)      && !ready_insn(ir) ) {
      if (no_free_reg(ir))
	flags |= FLAG_free;
      if (flags & FLAG_serialize) {
	rob[insns % history_depth].flags = flags | FLAG_decode;
	++cycle;
	return;
      }
      goto finish;
    }
  }
    
  // try to immediately execute new instruction
  if (ready_insn(ir)) {
    commit_insn(ir);
    uintptr_t jumped = perform(&ir, pc);
    flags |= FLAG_execute;
    History_t* h = &rob[insns++ % history_depth];
    *h = { ir, pc, i, cycle, flags };
    wheel[0][ index(latency[ir.opcode()]) ] = h;
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
  flags |= FLAG_busy;

  // try deferring instruction
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

 finish:
  try_issue_from_queue();
  ++cycle;
  rob[insns % history_depth] = { *i, pc, 0, cycle, FLAG_decode };
}

void core_t::try_issue_from_queue()
{
  unsigned flags = 0;
  for (int k=0; k<last; ++k) {
    History_t* h = queue[k];
    if (ready_insn(h->insn)) {
      wheel[0][ index(latency[h->insn.opcode()]) ] = h;
      (void)perform(&h->insn, h->pc); // no branches in queue
      //show_insn(q->insn, q->pc, &q->ref, flags);
      
      // remove from issue queue
      for (int j=k+1; j<last; ++j)
	queue[j-1] = queue[j]; // later instructions move up
      --last;			 // one slot
      break;
    }
  }
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
  // rename output register
  if (ir.rd() != 0 && ir.rd() != NOREG) {
    //release_reg(ir.rd());
    //if (--uses[regmap[ir.rd()]] == 0)
    //  freelist[numfree++] = regmap[ir.rd()];
    uint8_t old_rd = ir.rd();
    release_reg(regmap[old_rd]);
    ir.op_rd = freelist[--numfree];
    regmap[old_rd] = ir.rd();
    acquire_reg(ir.rd());
    busy[ir.rd()] = true;
  }
  ++outstanding;
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
  if (--uses[r] == 0)
    freelist[numfree++] = r;
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
