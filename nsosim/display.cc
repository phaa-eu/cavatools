#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"


// show opcode highlighting execution status
void History_t::show_opcode()
{
  extern const char* op_name[];
  if (flags & FLAG_execute) attron( A_REVERSE);
  int len = strlen(op_name[insn.opcode()]);
  printw("%s", op_name[insn.opcode()]);
  if (flags & FLAG_execute) attroff(A_REVERSE);
  printw("%*s", 12-len, "");
}

// show register with renaming and highlight status
void History_t::show_reg(char sep, int orig, int phys, bool busy[])
{
  extern const char* reg_name[];
  if (phys == NOREG) {
    printw("%c%s", sep, reg_name[orig]);
  }
  else {
    if (busy[phys]) attron( A_REVERSE);
    printw("%c%s(r%d)", sep, reg_name[orig], phys);
    if (busy[phys]) attroff(A_REVERSE);
  }
}

void History_t::display(bool busy[])
{
  if (pc==0 || insn.opcode()==Op_ZERO) {
    printw("*** nothing here ***");
    return;
  }

  if (flags == 0) attron(A_DIM);
  if (flags & FLAG_decode) attron(A_BOLD);
  
  printw("%c", (flags&FLAG_busy)	? 'b' : ' ');
  printw("%c", (flags&FLAG_qfull)	? 'f' : ' ');
  printw("%c", (flags&FLAG_jump)	? 'j' : ' ');
  printw("%c", (flags&FLAG_store)	? 's' : ' ');
  printw("%c", (flags&FLAG_serialize)	? '!' : ' ');
  printw("%c", (flags&FLAG_free)	? 'f' : ' ');
  printw("\t");
  
  char buf[256];;
  slabelpc(buf, pc);
  printw("%s", buf);
  
  uint32_t b = *(uint32_t*)pc;
  if (insn.compressed())
    printw("    %04x  ", b&0xFFFF);
  else
    printw("%08x  ",     b);
  show_opcode();
  char sep = ' ';
  if (ref) {
    if (insn.rd()  != NOREG)   { show_reg(sep, ref->rd(),  insn.rd() , busy); sep=','; }
    if (insn.rs1() != NOREG)   { show_reg(sep, ref->rs1(), insn.rs1(), busy); sep=','; }
    if (! insn.longimmed()) {
      if (insn.rs2() != NOREG) { show_reg(sep, ref->rs2(), insn.rs2(), busy); sep=','; }
      if (insn.rs3() != NOREG) { show_reg(sep, ref->rs3(), insn.rs3(), busy); sep=','; }
    }
  }
  else {
    if (insn.rd()  != NOREG)   { show_reg(sep, insn.rd(),  NOREG, busy); sep=','; }
    if (insn.rs1() != NOREG)   { show_reg(sep, insn.rs1(), NOREG, busy); sep=','; }
    if (! insn.longimmed()) {
      if (insn.rs2() != NOREG) { show_reg(sep, insn.rs2(), NOREG, busy); sep=','; }
      if (insn.rs3() != NOREG) { show_reg(sep, insn.rs3(), NOREG, busy); sep=','; }
    }
  }
  printw("%c%ld", sep, insn.immed());
  
  if (flags & FLAG_decode) attroff(A_BOLD);
  if (flags == 0) attroff(A_DIM);
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
    if (when < 0)
      continue;
    move(when%lines, 0);
    printw("%7ld ", when);
    if (when == rob[k].cycle) {
      rob[k].display(busy);
      k = (k-1+history_depth) % history_depth;
    }
    printw("\n");
  }
  refresh();
}

void core_t::interactive()
{
  initscr();                    /* Start curses mode */
  keypad(stdscr, true);         /* Need all keys */
  nonl();
  cbreak();                     /* Line buffering disabled */
  noecho();
  nodelay(stdscr, true);
  //start_color();
  
  bool freerun = false;
  for (;;) {
    int ch = getch();
    while (freerun && ch == ERR) {
      usleep(20000);
      if (freerun) {
	simulate_cycle();
	display_history();
      }
      ch = getch();
    }
    switch (ch) {
    case ERR:
      break;
    case 'q':
      endwin();
      return;
    case 's':
      freerun = false;
      simulate_cycle();
      display_history();
      break;
    case 'c':
      freerun = true;
      break;
    default:
      ;
    }
  }
}

  
