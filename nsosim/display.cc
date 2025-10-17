#include <cassert>
#include <unistd.h>
#include <ncurses.h>
#include <signal.h>
#include <setjmp.h>
#include <climits>

#include "caveat.h"
#include "hart.h"

#include "core.h"

const int msglines = 3;
static WINDOW* msgwin;

const int window_buffers = 100;
WINDOW* winbuf[window_buffers];
int frontwin = 0;
int behind = 0;


unsigned long number = 0;


void display_header(WINDOW* w, int y, int x)
{
  wmove(w, y, x);
  wattron(w, A_UNDERLINE);
  wprintw(w, "  cycle\tflags\t\tpc label\t       pc  hex insn  opcode\treg(renamed=uses), [stbuf]");
  wprintw(w, "%ld\n", number);
  wattroff(w, A_UNDERLINE);
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
void History_t::show_opcode(WINDOW* w, bool executing)
{
  extern const char* op_name[];
  if (executing) wattron(w,  A_REVERSE);
  int len = strlen(op_name[insn.opcode()]);
  wprintw(w, "%s", op_name[insn.opcode()]);
  if (executing) wattroff(w,  A_REVERSE);
  wprintw(w, "%*s", 23-len, "");
}

// show register with renaming and highlight status
//void History_t::show_reg(char sep, int orig, int phys, bool busy[])
void History_t::show_reg(WINDOW* w, char sep, int orig, int r, bool busy[], unsigned uses[])
{
  extern const char* reg_name[];
  wprintw(w, "%c", sep);
  if (busy[r]) wattron(w,  A_REVERSE);
  if (is_store_buffer(r))
    wprintw(w, "[sb%d=%d]", r-max_phy_regs, uses[r]);
  else
    wprintw(w, "%s(r%d=%d)", reg_name[orig], r, uses[r]);
  if (busy[r]) wattroff(w, A_REVERSE);
}

void show_flags(WINDOW* w, unsigned flags)
{
  wprintw(w, "%c", (flags&FLAG_busy)	? 'b' : ' ');
  wprintw(w, "%c", (flags&FLAG_qfull)	? 'f' : ' ');
  wprintw(w, "%c", (flags&FLAG_stuaddr)	? 'a' : ' ');
  wprintw(w, "%c", (flags&FLAG_stbfull)	? 's' : ' ');
  wprintw(w, "%c", (flags&FLAG_nofree)	? 'f' : ' ');
  wprintw(w, "%c", (flags&FLAG_serialize)	? '!' : ' ');
  wprintw(w, "\t");
}

void History_t::display(WINDOW* w, bool busy[], unsigned uses[])
{
  if (pc==0 || insn.opcode()==Op_ZERO) {
    wprintw(w, "*** nothing here ***");
    return;
  }

  if (status==STATUS_retired)  wattron(w, A_DIM);
  if (status==STATUS_dispatch) wattron(w, A_UNDERLINE);
  
  char buf[256];;
  slabelpc(buf, pc);
  wprintw(w, "%s", buf);

  if (status == STATUS_retired) {
    sdisasm(buf, pc, ref);
    wprintw(w, "%s", buf);
  }
  else {
    uint32_t b = *(uint32_t*)pc;
    if (insn.compressed())
      wprintw(w, "    %04x  ", b&0xFFFF);
    else
      wprintw(w, "%08x  ",     b);
    show_opcode(w, status==STATUS_execute);
    char sep = ' ';
    if (insn.rd()  != NOREG)   { show_reg(w, sep, ref->rd(),  insn.rd() , busy, uses); sep=','; }
    if (insn.rs1() != NOREG)   { show_reg(w, sep, ref->rs1(), insn.rs1(), busy, uses); sep=','; }
    if (! insn.longimmed()) {
      if (insn.rs2() != NOREG) { show_reg(w, sep, ref->rs2(), insn.rs2(), busy, uses); sep=','; }
      if (insn.rs3() != NOREG) { show_reg(w, sep, ref->rs3(), insn.rs3(), busy, uses); sep=','; }
    }
    wprintw(w, "%c%ld", sep, insn.immed());
  }
  
  if (status==STATUS_dispatch) wattroff(w, A_UNDERLINE);
  if (status==STATUS_retired)  wattroff(w, A_DIM);
}

void core_t::display_history(WINDOW* w, int y, int x, int lines)
{
  char buf[1024];

  long when = cycle;		    // cycle to be printed
  int k = insns % dispatch_history; // rob slot to display
  
  for (int l=0; l<lines-3; ++l, --when) { // leave a few blank lines
    if (when < 0)
      continue;
    wmove(w, y+when%lines, x);
    wprintw(w, "%7ld ", when);
    show_flags(w, cycle_flags[when % cycle_history]);
    if (when == rob[k].cycle) {
      rob[k].display(w, busy, uses);
      k = (k-1+dispatch_history) % dispatch_history;
    }
    wprintw(w, "\n");
  }
}

void display_membank(WINDOW* w, int y, int x, membank_t* m)
{
  wmove(w, y, x);
  if (! m->active()) {
    wprintw(w, "-");
    return;
  }
  if (is_store_buffer(m->rd)) {
    wattron(w,  A_BOLD);
    wprintw(w, "[sb%d]%ld", m->rd-max_phy_regs, m->finish-cycle);
    wattroff(w, A_BOLD);
  }
  else {
    wattron(w,  A_REVERSE);
    wprintw(w, "(r%d)%ld", m->rd, m->finish-cycle);
    wattroff(w, A_REVERSE);
  }
}
 
void display_memory(WINDOW* w, int y, int x)
{
  const int fieldwidth = 12;
  wclear(w);
  wmove(w, y, x);
  wprintw(w, "Channel");
  for (int k=0; k<memory_banks; ++k) {
    wmove(w, y, x+4+(k+1)*fieldwidth);
    wprintw(w, "bank[%d]", k);
  }
  for (int j=0; j<memory_channels; ++j) {
    wprintw(w, "%d ", j);
    display_membank(w, y+j+1, 3, &memport[j]);
    for (int k=0; k<memory_banks; ++k)
      display_membank(w, y+j+1, x+4+(k+1)*fieldwidth, &memory[j][k]);
  }
  //wnoutrefresh(w);
}


/*
 * Exception catching
 */

static sigjmp_buf bombed;

static void LocalExceptionHandler(int signum) {
  fprintf(stderr, "***** Caught exception %d\n", signum);
  siglongjmp(bombed, signum);
}

static struct sigaction localAction = { &LocalExceptionHandler, };

static void fastrun_without_display(core_t* cpu)
{
  struct sigaction sigsegv_buf, sigbus_buf, sigint_buf;
  // new actions
  sigaction(SIGSEGV, &localAction, &sigsegv_buf);
  sigaction(SIGBUS,  &localAction, &sigbus_buf);
  sigaction(SIGINT,  &localAction, &sigint_buf);
  // prepare to catch exception
  int signum;
  if (signum = sigsetjmp(bombed, 1)) {
    // comes here when exception happens
    const char* signame = 0;
    switch (signum) {
    case SIGTRAP:	signame = "SIGTRAP";	break;
    case SIGBUS:	signame = "SIGBUS";	break;
    case SIGSEGV:	signame = "SIGSEGV";	break;
    case SIGFPE:	signame = "SIGFPE";	break;
    case SIGINT:	signame = "SIGINT";	break;
    default:
      fprintf(stderr, "Caught exception %d\n", signum);
    }
    fprintf(stderr,"Caught exception %s\n", signame);
    sigaction(SIGSEGV, &sigsegv_buf, 0);
    sigaction(SIGBUS,  &sigbus_buf,  0);
    sigaction(SIGINT,  &sigint_buf,  0);

    refresh();
    wrefresh(winbuf[frontwin]);
    return;
  }
  endwin();
  for (;;) {
    cpu->simulate_cycle();
    frontwin = (frontwin+1) % window_buffers;
    WINDOW* w = winbuf[frontwin];
    wclear(w);
    display_memory(w, 0, 0);
    display_header(w, 2, 0);
    cpu->display_history(w, 3, 0, LINES-3);
    fprintf(stderr, "\r\33[2K%ld %ld", cycle, cpu->insns);
  } // infinite loop
}

#define remember_cycle() { frontwin=(frontwin+1)%window_buffers; WINDOW* w=winbuf[frontwin]; wclear(w); display_memory(w, 0, 0); display_header(w, 2, 0); display_history(w, 3, 0, LINES-5); wrefresh(w); }


void core_t::interactive()
{
  initscr();                    /* Start curses mode */
  keypad(stdscr, true);         /* Need all keys */
  nonl();
  cbreak();                     /* Line buffering disabled */
  noecho();
  nodelay(stdscr, true);
  //start_color();

  for (int k=0; k<window_buffers; ++k)
    winbuf[k] = newwin(LINES-msglines, COLS, 0, 0);

  //msgwin = newwin(msglines,		COLS,	LINES-msglines,	0);
  
  bool freerun = false;
  long framerate = 20000;
  //display_help();
  
  WINDOW* w = winbuf[frontwin];
  int ch;
  long unsigned stop_cycle = ULONG_MAX;
  //  for (int ch=getch(); ch!='q'; ch=getch()) {
  
 infinite_loop:
  while ((ch=getch()) == ERR) {
    if (freerun && cycle < stop_cycle) {
      simulate_cycle();
      remember_cycle();
      wrefresh(winbuf[frontwin]);
    }
    if (framerate)
      usleep(framerate);
  }
  
  if ('0' <= ch && ch <= '9') {
    number = 10*number + ch-'0';
    goto infinite_loop;
  }
  
  switch (ch) {
  case 'q':
    endwin();
    return;
  case 'h':
    help_screen();
    while ((ch=getch()) == ERR)
      usleep(100000);
    break;
  case 's':
    framerate = 20000;
    freerun = false;
    behind = 0;
    number = 0;
    simulate_cycle();
    remember_cycle();
    wrefresh(winbuf[frontwin]);
    break;
  case 'c':
    framerate = 20000;
    freerun = true;
    behind = 0;
    if (number) {
      stop_cycle = number;
      number = 0;
    }
    break;
  case 'C':
    framerate = 0;
    freerun = true;
    behind = 0;
    if (number) {
      stop_cycle = number;
      number = 0;
    }
    break;
  case 'g':
    fastrun_without_display(this);
    break;

  case 'b':
    if (behind < window_buffers)
      ++behind;
    else
      beep();
    overwrite(winbuf[ (frontwin-behind+window_buffers) % window_buffers ], stdscr);
    refresh();
    goto infinite_loop;
      
  case 'f':
    if (behind > 0)
      --behind;
    else
      beep();
    overwrite(winbuf[ (frontwin-behind+window_buffers) % window_buffers ], stdscr);
    refresh();
    goto infinite_loop;
  }
  goto infinite_loop;
}

  
// leftover junk
#if 0

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
#endif
