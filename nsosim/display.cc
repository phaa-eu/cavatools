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

void display_header()
{
  move(2, 0);
  attron(A_UNDERLINE);
  printw("  cycle\tflags\t\tpc label\t       pc  hex insn  opcode\treg(renamed=uses), [stbuf]");
  attroff(A_UNDERLINE);
}

void help_screen()
{
  clear();
  printw("Interactive Commands:\n");
  printw("  'q'\tquit\n");
  printw("  'h'\thelp, any key to go back\n");
  printw("  's'\tsingle step one cycle\n");
  printw("  'c'\tcontinuous run visibly, stop with 's'\n");
  printw("  'g'\tcontinuous run invisibly, no way to stop\n");
  printw("\n");
  printw("Cycle Flags:\n");
  printw("  'b'\tsource register(s) busy\n");
  printw("  'f'\tissue queue full\n");
  printw("  'a'\tunknown store address\n");
  printw("  's'\tstore buffer full\n");
  printw("  'f'\tregister freelist empty\n");;
  printw("  '!'\tflushing pipeline\n");
  printw("\n");
  refresh();
}
  

// show opcode highlighting execution status
void History_t::show_opcode(bool executing)
{
  extern const char* op_name[];
  if (executing) wattron(robwin,  A_REVERSE);
  int len = strlen(op_name[insn.opcode()]);
  wprintw(robwin, "%s", op_name[insn.opcode()]);
  if (executing) wattroff(robwin,  A_REVERSE);
  wprintw(robwin, "%*s", 23-len, "");
}

// show register with renaming and highlight status
//void History_t::show_reg(char sep, int orig, int phys, bool busy[])
void History_t::show_reg(char sep, int orig, int r, bool busy[], unsigned uses[])
{
  extern const char* reg_name[];
  wprintw(robwin, "%c", sep);
  if (busy[r]) wattron(robwin,  A_REVERSE);
  if (is_store_buffer(r))
    wprintw(robwin, "[sb%d=%d]", r-max_phy_regs, uses[r]);
  else
    wprintw(robwin, "%s(r%d=%d)", reg_name[orig], r, uses[r]);
  if (busy[r]) wattroff(robwin, A_REVERSE);
}

void show_flags(unsigned flags)
{
  wprintw(robwin, "%c", (flags&FLAG_busy)	? 'b' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_qfull)	? 'f' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_stuaddr)	? 'a' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_stbfull)	? 's' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_nofree)	? 'f' : ' ');
  wprintw(robwin, "%c", (flags&FLAG_serialize)	? '!' : ' ');
  wprintw(robwin, "\t");
}

void History_t::display(bool busy[], unsigned uses[])
{
  if (pc==0 || insn.opcode()==Op_ZERO) {
    wprintw(robwin, "*** nothing here ***");
    return;
  }

  if (status==STATUS_retired)  wattron(robwin, A_DIM);
  if (status==STATUS_dispatch) wattron(robwin, A_UNDERLINE);
  
  char buf[256];;
  slabelpc(buf, pc);
  wprintw(robwin, "%s", buf);

  if (status == STATUS_retired) {
    sdisasm(buf, pc, ref);
    wprintw(robwin, "%s", buf);
  }
  else {
    uint32_t b = *(uint32_t*)pc;
    if (insn.compressed())
      wprintw(robwin, "    %04x  ", b&0xFFFF);
    else
      wprintw(robwin, "%08x  ",     b);
    show_opcode(status==STATUS_execute);
    char sep = ' ';
    if (insn.rd()  != NOREG)   { show_reg(sep, ref->rd(),  insn.rd() , busy, uses); sep=','; }
    if (insn.rs1() != NOREG)   { show_reg(sep, ref->rs1(), insn.rs1(), busy, uses); sep=','; }
    if (! insn.longimmed()) {
      if (insn.rs2() != NOREG) { show_reg(sep, ref->rs2(), insn.rs2(), busy, uses); sep=','; }
      if (insn.rs3() != NOREG) { show_reg(sep, ref->rs3(), insn.rs3(), busy, uses); sep=','; }
    }
    wprintw(robwin, "%c%ld", sep, insn.immed());
  }
  
  if (status==STATUS_dispatch) wattroff(robwin, A_UNDERLINE);
  if (status==STATUS_retired)  wattroff(robwin, A_DIM);
}

void core_t::display_history()
{
  char buf[1024];
  int lines, cols;
  getmaxyx(robwin, lines, cols);
  if (cycle_history < lines)
    lines = cycle_history;
  wclear(robwin);

  long when = cycle;		    // cycle to be printed
  int k = insns % dispatch_history; // rob slot to display
  
  for (int l=0; l<lines-3; ++l, --when) { // leave a few blank lines
    if (when < 0)
      continue;
    wmove(robwin, when%lines, 0);
    wprintw(robwin, "%7ld ", when);
    show_flags(cycle_flags[when % cycle_history]);
    if (when == rob[k].cycle) {
      rob[k].display(busy, uses);
      k = (k-1+dispatch_history) % dispatch_history;
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

void display_memory()
{
  const int fieldwidth = 16;
  int lines, cols;
  getmaxyx(memwin, lines, cols);
  wclear(memwin);
  for (int k=0; k<memory_banks; ++k) {
    wmove(memwin, 0, k*fieldwidth);
    wprintw(memwin, "bank[%d]", k);
  }
  for (int j=0; j<memory_channels; ++j) {
    for (int k=0; k<memory_banks; ++k) {
      membank_t* m = &memory[j][k];
      if (! m->active())
	continue;
      wmove(memwin, j+1, k*fieldwidth);
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
  robwin = newwin(LINES-3-msglines,	COLS,	3,		0);
  msgwin = newwin(msglines,		COLS,	LINES-msglines,	0);
  
  bool freerun = false;
  long framerate = 20000;
  //display_help();
  for (int ch=getch(); ch!='q'; ch=getch()) {
    switch (ch) {
    case ERR:
      if (framerate)
	usleep(framerate);
      if (!freerun)
	continue;
      break;
    case 'h':
      help_screen();
      while ((ch=getch()) == ERR)
	usleep(100000);
      continue;
    case 's':
      framerate = 20000;
      freerun = false;
      break;
    case 'c':
      framerate = 20000;
      freerun = true;
      break;
    case 'f':
      framerate = 0;
      freerun = true;
      break;
    case 'g':
      endwin();
      for (;;) {
	simulate_cycle();
	fprintf(stderr, "\r\33[2K%ld %ld", cycle, insns);
      }
    default:
      continue;
    }
    simulate_cycle();

    display_memory();
    display_header();
    display_history();
    display_busy(busy);
    doupdate();
  }
  endwin();
}

  
