#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"





uintptr_t perform(Insn_t* i, uintptr_t pc);

void core_t::ooo_simulator() {
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
  uintptr_t pc = get_state();
  Header_t* bb = find_bb(pc);
  Insn_t* i = insnp(bb+1);

  // once per cycle
  for (cycle=0; ; ++cycle) {
    display_history();
  again:
    switch (getch()) {
    case ERR:
      usleep(100000);
      goto again;
    case 'q':
      return;
    case 'n':
      break;
    }
    
    // retire pipelined instruction(s)
    for (int k=0; k<num_write_ports; ++k) {
      History_t* h = wheel[k][index(0)];
      if (h) {
	Insn_t* p = &h->insn;
	busy[p->rd()] = false;	// destination register available
	wheel[k][index(0)]->flags &= ~(FLAG_delayed|FLAG_queue);
	wheel[k][index(0)]->flags |= FLAG_retire;
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

    Insn_t ir = *i;
    rename_input_regs(ir);
    
    // detect special cases
    unsigned flags = 0;
    ATTR_bv_t attr = attributes[ir.opcode()];
    if (attr & (ATTR_uj|ATTR_cj|ATTR_st|ATTR_ex)) {
      if (attr & (ATTR_uj|ATTR_cj))	flags |= FLAG_jump;
      if (attr &  ATTR_st)		flags |= FLAG_store;
      if (attr &  ATTR_ex)		flags |= FLAG_serialize;
    }
    
    if (flags || no_free_reg(ir)) {
      if ((flags & FLAG_serialize) && outstanding > 0 ||
	  (flags & FLAG_store)     && busy[ir.rs1()]  ||
	  (flags & FLAG_jump)      && !ready_insn(ir) ) {
	if (no_free_reg(ir))
	  flags |= FLAG_free;
	flags |= FLAG_stall;
	//show_insn(ir, pc, 0, flags);
	rob[cycle] = { ir, pc, i, flags };
	continue;
      }
    }
    
    // try to immediately execute new instruction
    if (ready_insn(ir)) {
      commit_insn(ir);
      History_t* h = &rob[insns++ % history_depth];
      *h = { ir, pc, i, cycle, flags };
      wheel[0][ index(latency[ir.opcode()]) ] = h;
      uintptr_t jumped = perform(&ir, pc);
      //show_insn(ir, pc, i, flags);
      
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
      continue;
    }
    flags |= FLAG_busy;

    // try deferring instruction
    if (last == issue_queue_length) {
      flags |= FLAG_qfull;
      //show_insn(*i, pc, 0, flags);
    }
    else {
      commit_insn(ir);
      History_t* h = &rob[insns++ % history_depth];
      *h = { ir, pc, i, cycle, flags };
      queue[last++] = h;
      flags |= FLAG_queue;
      show_insn(ir, pc, i, flags);
      // advance pc, can never be branch
      pc += ir.compressed() ? 2 : 4;
      if (++i >= insnp(bb+1)+bb->count) {
	bb = find_bb(pc);
	i = insnp(bb+1);
      }
    }

    try_issue_from_queue();
  } // once per cycle
}

void core_t::try_issue_from_queue()
{
  unsigned flags = 0;
  for (int k=0; k<last; ++k) {
    History_t* h = queue[k];
    if (ready_insn(h->insn)) {
      flags |= FLAG_delayed;
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
    release_reg(ir.rd());
    if (--uses[regmap[ir.rd()]] == 0)
      freelist[numfree++] = regmap[ir.rd()];
    ir.op_rd = freelist[--numfree];
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
    s.reg[k].x = h->s.xrf[k];
  for (int k=0; k<32; k++)
    s.reg[k+32].f = h->s.frf[k];
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


static int sdisreg(char* buf, char sep, int o, int n)
{
  return sprintf(buf, "%c%s(r%d)", sep, reg_name[o], n);
}

static int my_sdisasm(char* buf, const Insn_t* i, uintptr_t pc, const Insn_t* o)
{
  extern const char* reg_name[];
  int n = 0;
  if (i->opcode() == Op_ZERO) {
    n += sprintf(buf, "Nothing here");
    return n;
  }
  uint32_t b = *(uint32_t*)pc;
  if (i->compressed())
    n += sprintf(buf+n, "    %04x  ", b&0xFFFF);
  else
    n += sprintf(buf+n, "%08x  ",     b);
  n += sprintf(buf+n, "%-23s", op_name[i->opcode()]);
  char sep = ' ';
  if (i->rd()  != NOREG) { n += sdisreg(buf+n, sep, o->rd(),  i->rd() ); sep=','; }
  if (i->rs1() != NOREG) { n += sdisreg(buf+n, sep, o->rs1(), i->rs1()); sep=','; }
  if (i->longimmed())    { n += sprintf(buf+n, "%c%ld", sep, i->immed()); }
  else {
    if (i->rs2() != NOREG) { n += sdisreg(buf+n, sep, o->rs2(), i->rs2()); sep=','; }
    if (i->rs3() != NOREG) { n += sdisreg(buf+n, sep, o->rs3(), i->rs3()); sep=','; }
    n += sprintf(buf+n, "%c%ld", sep, i->immed());
  }
  return n;
}

static void my_disasm(const Insn_t ir, uintptr_t pc, const Insn_t ref)
{
  char buffer[1024];
  my_sdisasm(buffer, &ir, pc, &ref);
  fprintf(stderr, "%s\n", buffer);
}

void core_t::show_insn(Insn_t ir, uintptr_t pc, Insn_t* ref, unsigned flags)
{
  static long unsigned last_cycle = 99;
  if (cycle != last_cycle)
    fprintf(stderr, "%7ld ", cycle);
  else
    fprintf(stderr, "\t");
  last_cycle = cycle;
  fprintf(stderr, "%c", (flags&FLAG_retire)	? 'R' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_delayed)	? 'D' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_queue)	? 'Q' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_busy)	? 'b' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_qfull)	? 'f' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_jump)	? 'j' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_store)	? 's' : ' ');
  fprintf(stderr, "%c", (flags&FLAG_serialize)	? '!' : ' ');
  fprintf(stderr, "\t");
  labelpc(pc, stderr);
  if (ref)
    my_disasm(ir, pc, *ref);
  else
    disasm(pc, &ir);
}

void core_t::display_history()
{
  char buf[1024];
  int lines = (LINES<history_depth ? LINES : history_depth);
  clear();
  int k = (insns-lines+history_depth) % history_depth;
  for (int l=0; l<lines; ++l) {
    k = (k+1) % history_depth;
    History_t* h = &rob[k];
    if (h->insn.opcode() == Op_ZERO)
      printw(".\n");
    else {
      printw("%7ld ", h->cycle);
      unsigned flags = h->flags;
      printw("%c", (flags&FLAG_retire)	? 'R' : ' ');
      printw("%c", (flags&FLAG_delayed)	? 'D' : ' ');
      printw("%c", (flags&FLAG_queue)	? 'Q' : ' ');
      printw("%c", (flags&FLAG_busy)	? 'b' : ' ');
      printw("%c", (flags&FLAG_qfull)	? 'f' : ' ');
      printw("%c", (flags&FLAG_jump)	? 'j' : ' ');
      printw("%c", (flags&FLAG_store)	? 's' : ' ');
      printw("%c", (flags&FLAG_serialize)	? '!' : ' ');
      printw("\t");
      slabelpc(buf, h->pc);
      printw("%s", buf);
      my_sdisasm(buf, &h->insn, h->pc, h->ref);
      printw("%s\n", buf);
    }
  } while (k != insns);
  refresh();
}
