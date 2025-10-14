#include <cassert>
#include <unistd.h>
#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"

const int msglines = 3;

static WINDOW* memwin;
static WINDOW* robwin;
static WINDOW* msgwin;

// show opcode highlighting execution status
void History_t::show_opcode()
{
  extern const char* op_name[];
  if (flags & FLAG_execute) wattron(robwin,  A_REVERSE);
  int len = strlen(op_name[insn.opcode()]);
  wprintw(robwin, "%s", op_name[insn.opcode()]);
  if (flags & FLAG_execute) wattroff(robwin, A_REVERSE);
  wprintw(robwin, "%*s", 12-len, "");
}

// show register with renaming and highlight status
//void History_t::show_reg(char sep, int orig, int phys, bool busy[])
void History_t::show_reg(char sep, int orig, int phys, bool busy[], unsigned uses[])
{
  extern const char* reg_name[];
  wprintw(robwin, "%c", sep);
  if (orig == NOREG) {
    if (phys < max_phy_regs)
      wprintw(robwin, "(r%d=%d)", phys, uses[phys]);
    else
      wprintw(robwin, "[sb%d=%d]", phys-max_phy_regs, uses[phys]);
  }
  else {
    if (busy[phys]) wattron(robwin,  A_REVERSE);
    if (phys < max_phy_regs)
      //wprintw(robwin, "%s(r%d)", reg_name[orig], phys);
      wprintw(robwin, "%s(r%d=%d)", reg_name[orig], phys, uses[phys]);
    else
      //wprintw(robwin, "(sb%d)", orig-max_phy_regs);
      wprintw(robwin, "[sb%d=%d]", phys-max_phy_regs, uses[phys]);
    if (busy[phys]) wattroff(robwin, A_REVERSE);
  }
}

void History_t::display(bool busy[], unsigned uses[])
{
  if (pc==0 || insn.opcode()==Op_ZERO) {
    wprintw(robwin, "*** nothing here ***");
    return;
  }

  if (flags == 0) wattron(robwin, A_DIM);
  if (flags & FLAG_decode) wattron(robwin, A_BOLD);
  
  wprintw(robwin, "%c", (flags&FLAG_busy)	? 'b' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_qfull)	? 'f' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_staddr)	? 'a' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_stbuf)	? 's' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_serialize)	? '!' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_free)	? 'f' : ' ');
  wprintw(robwin, "\t");
  
  char buf[256];;
  slabelpc(buf, pc);
  wprintw(robwin, "%s", buf);
  
  uint32_t b = *(uint32_t*)pc;
  if (insn.compressed())
    wprintw(robwin, "    %04x  ", b&0xFFFF);
  else
    wprintw(robwin, "%08x  ",     b);
  show_opcode();
  char sep = ' ';
  if (ref) {
    if (insn.rd()  != NOREG)   { show_reg(sep, ref->rd(),  insn.rd() , busy, uses); sep=','; }
    if (insn.rs1() != NOREG)   { show_reg(sep, ref->rs1(), insn.rs1(), busy, uses); sep=','; }
    if (! insn.longimmed()) {
      if (insn.rs2() != NOREG) { show_reg(sep, ref->rs2(), insn.rs2(), busy, uses); sep=','; }
      if (insn.rs3() != NOREG) { show_reg(sep, ref->rs3(), insn.rs3(), busy, uses); sep=','; }
    }
  }
  else {
    if (insn.rd()  != NOREG)   { show_reg(sep, NOREG, insn.rd(),  busy, uses); sep=','; }
    if (insn.rs1() != NOREG)   { show_reg(sep, NOREG, insn.rs1(), busy, uses); sep=','; }
    if (! insn.longimmed()) {
      if (insn.rs2() != NOREG) { show_reg(sep, NOREG, insn.rs2(), busy, uses); sep=','; }
      if (insn.rs3() != NOREG) { show_reg(sep, NOREG, insn.rs3(), busy, uses); sep=','; }
    }
  }
  wprintw(robwin, "%c%ld", sep, insn.immed());
  
  if (flags & FLAG_decode) wattroff(robwin, A_BOLD);
  if (flags == 0) wattroff(robwin, A_DIM);
}

void core_t::display_history()
{
  char buf[1024];
  int lines, cols;
  getmaxyx(robwin, lines, cols);
  if (history_depth < lines)
    lines = history_depth;
  wclear(robwin);

  long when = cycle;		     // cycle to be printed
  //int k = (insns-1) % history_depth; // rob slot to display
  int k = insns % history_depth; // rob slot to display
  for (int l=0; l<lines-3; ++l, --when) {    // leave a few blank lines
    if (when < 0)
      continue;
    wmove(robwin, when%lines, 0);
    wprintw(robwin, "%7ld ", when);
    if (when == rob[k].cycle) {
      rob[k].display(busy, uses);
      k = (k-1+history_depth) % history_depth;
    }
    wprintw(robwin, "\n");
  }
  //wrefresh(robwin);
  wnoutrefresh(robwin);
}

void show_number_vertical(WINDOW* w, int y, int x, int n, int digits)
{
  char digit[digits];
  for (int k=0; k<digits; ++k) {
    digit[k] = n % 10 + '0';
    n /= 10;
  }
  for (int k=digits-1; k>=0; k--)
    if (digit[k] == '0')
      digit[k] = ' ';
    else
      break;
  for (int k=0; k<digits; ++k)
    mvwprintw(w, y+digits-1-k, x, "%c", digit[k]);
}

void display_busy(bool busy[])
{
  int lines, cols;
  getmaxyx(msgwin, lines, cols);
  wclear(msgwin);
  for (int k=0; k<max_phy_regs; ++k) {
    if (!busy[k])
      continue;
    wattron( msgwin, A_REVERSE);
    show_number_vertical(msgwin, 0, k, k, lines);
    wattroff(msgwin, A_REVERSE);
  }
  wnoutrefresh(msgwin);
}

void display_memory(membank_t memory[])
{
  const int fieldwidth = 16;
  int lines, cols;
  getmaxyx(memwin, lines, cols);
  wclear(memwin);
  for (int k=0; k<membank_number; ++k) {
    wmove(memwin, 0, k*fieldwidth);
    wprintw(memwin, "bank[%d]", k);
  }
  for (int k=0; k<membank_number; ++k) {
    membank_t* m = &memory[k];
    if (m->rd) {
      wmove(memwin, 1, k*fieldwidth);
      if (is_store_buffer(m->rd)) {
	wattron(memwin,  A_BOLD);
	wprintw(memwin, "[sb%d]%ld", m->rd-max_phy_regs, m->finish-cycle);
	wattroff(memwin, A_BOLD);
      }
      else {
	wattron(memwin,  A_REVERSE);
	wprintw(memwin, "(r%d)%ld", m->rd, m->finish-cycle);
	wattroff(memwin, A_REVERSE);
      }
    }
  }
  wnoutrefresh(memwin);
}

#define display_screen() { display_memory(memory); display_history(); display_busy(busy); doupdate(); }

void core_t::interactive()
{
  initscr();                    /* Start curses mode */
  keypad(stdscr, true);         /* Need all keys */
  nonl();
  cbreak();                     /* Line buffering disabled */
  noecho();
  nodelay(stdscr, true);
  //start_color();

  memwin = newwin(2,			COLS,	0,		0);
  robwin = newwin(LINES-2-msglines,	COLS,	2,		0);
  msgwin = newwin(msglines,		COLS,	LINES-msglines,	0);
  
  bool freerun = false;
  display_screen();
  for (;;) {
    int ch = getch();
    while (freerun && ch == ERR) {
      usleep(20000);
      if (freerun) {
	simulate_cycle();
	display_screen();
	++cycle;
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
      display_screen();
      break;
    case 'c':
      freerun = true;
      break;
    case 'g':
      endwin();
      for (;;) {
	simulate_cycle();
	++cycle;
	fprintf(stderr, "\r\33[2K%ld %ld", cycle, insns);
      }
    default:
      ;
    }
  }
}

  
