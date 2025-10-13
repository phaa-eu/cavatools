#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"



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

void core_t::display_history()
{
  char buf[1024];
  int lines = (LINES<history_depth ? LINES : history_depth);
  clear();

  long when = cycle;		     // cycle to be printed
  //int k = (insns-1) % history_depth; // rob slot to display
  int k = insns % history_depth; // rob slot to display
  for (int l=0; l<lines-3; ++l, --when) {    // leave a few blank lines
    History_t* h = &rob[k];
    move((when+lines)%lines, 0);	// line for this cycle number
    if (when != h->cycle)
      printw(".\n");
    else {
      printw("%7ld ", when);
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
      if (h->ref)
	my_sdisasm(buf, &h->insn, h->pc, h->ref);
      else
	sdisasm(buf, pc, &h->insn);
      printw("%s\n", buf);
      k = (k-1+history_depth) % history_depth;
    }
  }
  refresh();
}
