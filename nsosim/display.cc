#include <cassert>
#include <unistd.h>
#include <ncurses.h>
#include <signal.h>
#include <setjmp.h>
#include <climits>

#include "caveat.h"
#include "hart.h"

#include "memory.h"
#include "components.h"
#include "core.h"

const int msglines = 3;
static WINDOW* msgwin;


unsigned long number = 0;



// show opcode highlighting execution status
//static void show_opcode(WINDOW* w, Opcode_t op, bool executing)
static void show_opcode(WINDOW* w, Opcode_t op, History_t::Status_t status)
{
  extern const char* op_name[];
  bool executing = status==History_t::Executing;
  if (executing) wattron(w,  A_REVERSE);
  int len = strlen(op_name[op]);
  wprintw(w, "%s", op_name[op]);
  if (executing) wattroff(w,  A_REVERSE);
  
  switch (status) {
  case History_t::Queued_stbchk:  wprintw(w, "?"); ++len; break;
  case History_t::Queued_noport:  wprintw(w, "*"); ++len; break;
  case History_t::Queued_nochk:   wprintw(w, "@"); ++len; break;
  }
  wprintw(w, "%*s", 23-len, "");
}

// show register with renaming and highlight status
//void History_t::show_reg(char sep, int orig, int phys, bool busy[])
void Core_t::show_reg(WINDOW* w, Reg_t n, char sep, int ref)
{
  extern const char* reg_name[];
  if (ref == NOREG)
    return;
  wprintw(w, "%c", sep);
  if (regs.busy(n)) wattron(w,  A_REVERSE);
  if (is_store_buffer(n))
    wprintw(w, "[sb%d=%d]", n-max_phy_regs, regs.uses(n));
  else
    wprintw(w, "%s(r%d=%d)", reg_name[ref], n, regs.uses(n));
  if (regs.busy(n)) wattroff(w, A_REVERSE);
}



void History_t::display(WINDOW* w, Core_t* c)
{
  switch (status) {
  case History_t::Retired:	wattron(w, A_DIM); break;
  case History_t::Dispatch:	wattron(w, A_UNDERLINE); break;
    //  case History_t::Executing:	wattron(w, COLOR_PAIR(1));
    //  default:			wattron(w, COLOR_PAIR(2));
  }

#ifdef VERIFY
  extern const char* reg_name[];
  if (ref.rd() == NOREG) {
    if (attributes[ref.opcode()] & ATTR_st)
      wprintw(w, "%4s[%16lx %16lx] ", "data", expected_rd, actual_rd);
    else
      wprintw(w, "%40s", "");
  }
  else
    wprintw(w, "%4s[%16lx %16lx] ", reg_name[ref.rd()], expected_rd, actual_rd);
  bool mismatch = status==History_t::Retired ? actual_rd != expected_rd : false;
  if (mismatch) wattron(w, A_REVERSE);
#endif
  
  char buf[256];;
  slabelpc(buf, pc);
  wprintw(w, "%s", buf);
  
#ifdef VERIFY
  wprintw(w, "=?%8lx: ", expected_pc);
#endif

  if (status == History_t::Retired) {
    sdisasm(buf, pc, &ref);
    wprintw(w, "%s", buf);
  }
  else {
    uint32_t b = *(uint32_t*)pc;
    if (insn.compressed())
      wprintw(w, "    %04x  ", b&0xFFFF);
    else
      wprintw(w, "%08x  ",     b);
    //show_opcode(w, insn.opcode(), status==History_t::Executing);
    show_opcode(w, insn.opcode(), status);

    if (status == History_t::Dispatch) {
      if (insn.rd()!=NOREG) wprintw(w, " %s", reg_name[ref.rd()]);
    }
    else {
      c->show_reg(w, insn.rd(),  ' ', ref.rd());
    }
    char sep = '=';
    
    if (status == History_t::Executing) {
      if (insn.rs1()!=NOREG) wprintw(w, "%c%s", sep, reg_name[ref.rs1()]), sep=',';
      if (! insn.longimmed()) {
	if (insn.rs2()!=NOREG) wprintw(w, "%c%s", sep, reg_name[ref.rs2()]), sep=',';
	if (insn.rs3()!=NOREG) wprintw(w, "%c%s", sep, reg_name[ref.rs3()]), sep=',';
      }
    }
    else {
      c->show_reg(w, insn.rs1(), sep, ref.rs1()), sep=',';
      if (! insn.longimmed()) {
	c->show_reg(w, insn.rs2(), sep, ref.rs2()), sep=',';
	c->show_reg(w, insn.rs3(), sep, ref.rs3()), sep=',';
      }
    }
    wprintw(w, "%c%ld", sep, insn.immed());

    if (stbpos != NOREG) {
    //if (attributes[insn.opcode()] & (ATTR_ld|ATTR_st)) {
      if (c->regs.busy(stbpos)) wattron(w,  A_REVERSE);
      wprintw(w, "\tsb%d=%d", stbpos-max_phy_regs, c->regs.uses(stbpos));
      if (c->regs.busy(stbpos)) wattroff(w,  A_REVERSE);
      wprintw(w, "[%16lx]", c->s.reg[stbpos].a);
    }
  }

#ifdef VERIFY
  if (mismatch) wattroff(w, A_REVERSE);
#endif
  switch (status) {
  case History_t::Retired:	wattroff(w, A_DIM); break;
  case History_t::Dispatch:	wattroff(w, A_UNDERLINE); break;
    //  case History_t::Executing:	wattroff(w, COLOR_PAIR(1));
    //  default:			wattroff(w, COLOR_PAIR(2));
  }
  //wattron(w, COLOR_PAIR(0));
  //wprintw(w, "\n");
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

static void show_flags(WINDOW* w, unsigned flags)
{
  wprintw(w, "%c", (flags&FLAG_busy)		? 'b' : ' ');
  wprintw(w, "%c", (flags&FLAG_regbus)		? 'r' : ' ');
  wprintw(w, "%c", (flags&FLAG_qfull)		? 'f' : ' ');
  wprintw(w, "%c", (flags&FLAG_stuaddr)		? 'a' : ' ');
  wprintw(w, "%c", (flags&FLAG_stbfull)		? 's' : ' ');
  wprintw(w, "%c", (flags&FLAG_nofree)		? 'f' : ' ');
  wprintw(w, "%c", (flags&FLAG_serialize)	? '!' : ' ');
  wprintw(w, "%c", (flags&FLAG_stbhit)		? 'h' : ' ');
  wprintw(w, "%c", (flags&FLAG_endmem)		? 'm' : ' ');
  wprintw(w, "%c", (flags&FLAG_noport)		? 'p' : ' ');
  wprintw(w, "%c", (flags&FLAG_stbchk)		? 'c' : ' ');
  wprintw(w, "\t");
}

void display_history(WINDOW* w, int y, int x, Core_t* c, int lines)
{
  wmove(w, y, 0);
  wprintw(w, "sb[");
  for (int k=0; k<store_buffer_length; ++k) {
    if (c->regs.busy(k+max_phy_regs)) wattron(w, A_REVERSE);
    int u = c->regs.uses(k+max_phy_regs);
    assert(u >= 0);
    wprintw(w, "%c", u==0 ? ' ' : u>9 ? '*' : u+'0');
    if (c->regs.busy(k+max_phy_regs)) wattroff(w, A_REVERSE);
  }
  wprintw(w, "]    busy[");
  for (int k=0; k<max_phy_regs; ++k) {
    if (c->regs.busy(k)) wattron(w, A_REVERSE);
    int u = c->regs.uses(k);
    assert(u >= 0);
    wprintw(w, "%c", u==0 ? ' ' : u>9 ? '*' : u+'0');
    if (c->regs.busy(k)) wattroff(w, A_REVERSE);
  }
  wprintw(w, "]\n");
  --lines;			    // account for register busy line
  ++y;

  wmove(w, y, 0);
  wprintw(w, "wheel=[");
  for (int k=0; k<max_latency+1; ++k) {
    wprintw(w, "%c", c->regs.wheel[k] ? '*' : '.');
  }
  wprintw(w, "]\n");
  --lines;
  ++y;

  wmove(w, y, x);
  wattron(w, A_UNDERLINE);
  wprintw(w, "  cycle\tflags\t");
#ifdef VERIFYf
  wprintw(w, "\t     Expected\t\tActual\t");
#endif
  wprintw(w, "\tpc label\t       pc  hex insn  opcode\t\treg(renamed=uses), [stbuf]");
  wprintw(w, "  inflight=%lld, last=%d, stbtail=%d", c->inflight(), c->last, c->regs.stbtail);
#ifdef VERIFY
  if (mismatches > 0) {
    wattron(w, A_REVERSE|A_BLINK);
    wprintw(w, " mismatched=%lld", mismatches);
    wattroff(w, A_REVERSE|A_BLINK);
  }
#endif
  wprintw(w, "\n");
  wattroff(w, A_UNDERLINE);
  --lines;			    // account for header line
  ++y;

  char buf[1024];
  long when = cycle;		    // cycle to be printed
  int k = c->insns() % dispatch_history; // rob slot to display
  for (int l=0; l<lines-3; ++l, --when) { // leave a few blank lines
    if (when < 0)
      continue;
    wmove(w, y+when%lines, x);
    wprintw(w, "%7ld ", when);
    show_flags(w, c->cycle_flags[when % cycle_history]);
    if (when == c->rob[k].clock) {
      c->rob[k].display(w, c);
      k = (k-1+dispatch_history) % dispatch_history;
    }
    wprintw(w, "\n");
  }
}

void Memory_t::display(WINDOW* w, int y, int x)
{
  wmove(w, y, x);
  if (! active()) {
    wprintw(w, "-");
    return;
  }

  History_t* h = history();
  Insn_t ir = h->insn;
  unsigned win_attr = 0;
  if (attributes[ir.opcode()] & ATTR_ld)           win_attr |= A_BOLD;
  if (attributes[ir.opcode()] & ATTR_st)           win_attr |= A_REVERSE;
  if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) win_attr |= A_UNDERLINE;
  wattron(w, win_attr);

  if (ir.rd() != NOREG)
    wprintw(w, "(r%d)", ir.rd());
  if (h->stbpos != NOREG)
    wprintw(w, "[sb%d]", h->stbpos-max_phy_regs);
  wprintw(w, "%d", -(int)((finish()-cycle)));

  wattroff(w, win_attr);
}

void Port_t::display(WINDOW* w, int y, int x, Core_t* c)
{
  wmove(w, y, x);
  if (!active()) {
    wprintw(w, "(nothing)");
    return;
  }
  
  History_t* h = history();
  Insn_t ir = h->insn;

  //wprintw(w, "r%d", ir.rd());
  c->show_reg(w, ir.rd(), ' ', h->ref.rd());
  if (h->stbpos != NOREG)
    wprintw(w, ",sb%d", h->stbpos-max_phy_regs);
  wprintw(w, "[%d]", mem_bank(addr()));
  wprintw(w, "+%d", latency());
  return;

  unsigned win_attr;
  if (attributes[ir.opcode()] & ATTR_ld)           win_attr |= A_BOLD;
  if (attributes[ir.opcode()] & ATTR_st)           win_attr |= A_REVERSE;
  if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) win_attr |= A_UNDERLINE;
  wattron(w, win_attr);
  
  if (h->stbpos != NOREG) {
    wprintw(w, "[sb%d]", h->stbpos-max_phy_regs);
    wprintw(w, "+%d", latency());
  }
  wattroff(w, win_attr);
}

 
void display_memory_system(WINDOW* w, int y, int x)
{
  const int fieldwidth = 20;
  wmove(w, y, x);
  wprintw(w, "Channel");
  for (int k=0; k<memory_banks; ++k) {
    wmove(w, y, x+8+k*fieldwidth);
    wprintw(w, "bank[%d]", k);
  }
  for (int j=0; j<memory_channels; ++j) {
    wmove(w, y+j+1, 0);
    wprintw(w, "%7d ", j);
    for (int k=0; k<memory_banks; ++k)
      memory[j][k].display(w, y+1+j, x+8+k*fieldwidth);
  }
}






// leftover junk
#if 0


f/*
 * Exception catching
 */

static sigjmp_buf bombed;

static void LocalExceptionHandler(int signum) {
  fprintf(stderr, "***** Caught exception %d\n", signum);
  siglongjmp(bombed, signum);
}

static struct sigaction localAction = { &LocalExceptionHandler, };

static void fastrun_without_display(Core_t* cpu)
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
    return;
  }
  endwin();
  for (;;) {
    cpu->simulate_cycle();
    frontwin = (frontwin+1) % window_buffers;
    WINDOW* w = winbuf[frontwin];
    wclear(w);
    display_memory_system(w, 0, 0);
    cpu->port.display(w, 2, 0, cpu);
    cpu->display_history(w, 3, 0, LINES-2);
    fprintf(stderr, "\r\33[2K%ld %ld", cycle, cpu->insns);
  } // infinite loop
}




#define remember_cycle() { frontwin=(frontwin+1)%window_buffers; WINDOW* w=winbuf[frontwin]; wclear(w); display_memory(w, 0, 0); display_header(w, 2, 0); display_history(w, 3, 0, LINES-5); wrefresh(w); }

void core_t::safe_cycle()
{
  hart_t* h = this;
  single_step();		// use uspike state
  expected = i->rd()==NOREG ? ~0 : h->s.xrf[i->rd()];
  
  simulate_cycle();
  remember_cycle();
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
      safe_cycle();
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
      
  case 'f':
    if (behind > 0) {
      --behind;
      overwrite(winbuf[ (frontwin-behind+window_buffers) % window_buffers ], stdscr);
      refresh();
      goto infinite_loop;
    }
    // fall into single step!
  case 's':
    framerate = 20000;
    freerun = false;
    behind = 0;
    number = 0;
    safe_cycle();
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
  }
  goto infinite_loop;
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
#endif
