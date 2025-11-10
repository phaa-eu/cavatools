#include <cassert>
#include <unistd.h>
#include <ncurses.h>
#include <signal.h>
#include <setjmp.h>
#include <climits>

#include "caveat.h"
#include "hart.h"

#include "components.h"
#include "memory.h"
#include "core.h"
#include "display.h"

bool wide_display = false;


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
  if (wide_display) {
    extern const char* reg_name[];
    bool bad_rd = status==History_t::Retired && actual_rd!=expected_rd;
    if (ref.rd() == NOREG) {
      if (attributes[ref.opcode()] & ATTR_st) {
	if (bad_rd) wattron(w, A_REVERSE);
	wprintw(w, "%4s[%16lx %16lx] ", "data", expected_rd, actual_rd);
	if (bad_rd) wattroff(w, A_REVERSE);
      }
      else
	wprintw(w, "%40s", "");
    }
    else {
      if (bad_rd) wattron(w, A_REVERSE);
      wprintw(w, "%4s[%16lx %16lx] ", reg_name[ref.rd()], expected_rd, actual_rd);
      if (bad_rd) wattroff(w, A_REVERSE);
    }
  }
  //bool mismatch = status==History_t::Retired ? actual_rd != expected_rd : false;
  //if (mismatch) {
  //  wattron(w, A_REVERSE);
  //  wide_display = true;
  //}
#endif
  
  char buf[256];;
  slabelpc(buf, pc);

#ifdef VERIFY
  bool bad_pc = status==History_t::Retired && pc!=expected_pc;
  if (bad_pc) wattron(w, A_REVERSE);
  wprintw(w, "%s", buf);
  if (wide_display) {
    wprintw(w, "=?%8lx: ", expected_pc);
  }
  if (bad_pc) wattroff(w, A_REVERSE);
#else
  wprintw(w, "%s", buf);
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
      if (c->regs.busy(stbpos)) wattron(w,  A_REVERSE);
      wprintw(w, ";  [sb%d=%d]", stbpos-max_phy_regs, c->regs.uses(stbpos));
      if (c->regs.busy(stbpos)) wattroff(w,  A_REVERSE);
      if (wide_display)
	wprintw(w, "[%16lx]", c->s.reg[stbpos].a);
    }
  }

#ifdef VERIFY
  //if (mismatches) wattroff(w, A_REVERSE);
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
  printw("  'r'\tregister write bus unavailable\n");
  printw("  'f'\tissue queue full\n");
  printw("  'a'\tunknown store address\n");
  printw("  's'\tstore buffer full\n");
  printw("  'f'\tregister freelist empty\n");;
  printw("  '!'\tflushing pipeline\n");
  printw("  'h'\tstore buffer hit\n");
  printw("  'm'\tmemory bank finished\n");
  printw("  'p'\tmemory port busy\n");
  printw("  'c'\tstore buffer check\n");
  printw("\n");
  refresh();
}

const char* reason_name[] = {
  "-",
  "",				// no clutter
  "Regs",
  "Bus",
  "Free",
  "Qfull",
  "SBfull",
  "Addr",
  "Bregs",
  "Bbus",
  "Flush",
  "Jumped",
  "Port",
  "Check",
  "WAW",
};

void Core_t::display_stall_reasons(WINDOW* w, int y, int x)
{
  wmove(w, y, x);
  wprintw(w, "Reason  Dispatch  Execute\n");
  for (int k=0; k<(int)Number_of_Reasons; ++k) {
    const char* name;
    switch (k) {
    Idle:	name = "Idle";		break;
    Ready:	name = "Ready";		break;
    default:	name = reason_name[k];	break;
    }
    wprintw(w, "%-7s %8.2f %8.2f\n", name,
	    100.0*dispatch_stalls[k]/cycle,
	    100.0*execute_stalls [k]/cycle);
  }
}

static void show_flags(WINDOW* w, Reason_t dispatch, Reason_t execute)
{
  wprintw(w, "%-7s %-7s", reason_name[dispatch], reason_name[execute]);
  wprintw(w, "\t");
}

void display_history(WINDOW* w, int y, int x, Core_t* c, int lines)
{
  wmove(w, y, 0);
  if (wide_display) {
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
  }

  wmove(w, y, x);
  wattron(w, A_UNDERLINE);
  wprintw(w, "  cycle\tDispatch Execute\t");
#ifdef VERIFY
  if (wide_display)
    wprintw(w, "     Expected\t\tActual\t");
#endif
  wprintw(w, "\tpc label\t\t       pc  hex insn  opcode\t\treg(renamed=uses), [stbuf]");
  if (wide_display)
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
    show_flags(w, c->not_dispatch[when % cycle_history], c->not_execute[when % cycle_history]);

    //if (c->not_dispatch[when % cycle_history] == Br_jumped)
    //  wprintw(w, "%16lx", c->rob[k+1].expecfted_rd);
    
    if (when == c->rob[k].clock) {
#ifdef PRETTY
      switch (c->rob[k].status) {
      case History_t::Dispatch:		color_dispatch();	break;
      case History_t::Immediate:	color_immediate();	break;
      case History_t::Queued:
      case History_t::Queued_stbchk:
      case History_t::Queued_noport:
      case History_t::Queued_nochk:	color_queued();		break;
      case History_t::Executing:	color_execute();	break;
      case History_t::Retired:		color_retired();	break;
      }
#endif
      
      c->rob[k].display(w, c);
      k = (k-1+dispatch_history) % dispatch_history;
      
#ifdef PRETTY
      color_default();
#endif
    }
    //wprintw(w, "\n");
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
  if (last == 0) {
    wmove(w, 0, x);
    wprintw(w, "(empty)");
    return;
  }

  for (int k=0; k<last; ++k) {
    wmove(w, y+k, x);
    wprintw(w, "[%d]", k);
    
    History_t* h = queue[k].history;
    Insn_t ir = h->insn;

    char sep = ' ';
    if (ir.rd()) {
      c->show_reg(w, ir.rd(), sep, h->ref.rd());
      sep = ',';
    }
    if (h->stbpos != NOREG)
      wprintw(w, "%csb%d", sep, h->stbpos-max_phy_regs);
    wprintw(w, "[%d]", mem_bank(queue[k].addr));
    wprintw(w, "+%d", queue[k].latency);

#if 0
    unsigned win_attr;
    if (attributes[ir.opcode()] & ATTR_ld)           win_attr |= A_BOLD;
    if (attributes[ir.opcode()] & ATTR_st)           win_attr |= A_REVERSE;
    if (attributes[ir.opcode()] & (ATTR_ld|ATTR_st)) win_attr |= A_UNDERLINE;
    wattron(w, win_attr);
  
    if (h->stbpos != NOREG) {
      wprintw(w, "[sb%d]", h->stbpos-max_phy_regs);
      wprintw(w, "+%d", queue[k].latency);
    }
    wattroff(w, win_attr);
#endif
    
  }
}

 
void display_memory_system(WINDOW* w, int y0, int x0)
{  
  int full_lines  = (memory_banks-1) / banks_per_line;
  int final_banks = memory_banks - banks_per_line*full_lines;
  int labels = full_lines>0 ? banks_per_line : memory_banks;
  
  for (int j=0; j<labels; ++j) {
    wmove(w, y0, port_width + j*bank_width);
    wprintw(w, "bank");
    char sep = '[';
    for (int i=0; i<full_lines; ++i) {
      wprintw(w, "%c%d", sep, i*banks_per_line+j);
      sep = ',';
    }
    wprintw(w, "%c%d]", sep, full_lines*banks_per_line+j);
  }

  for (int i=0; i<full_lines; ++i) {
    for (int j=0; j<banks_per_line; ++j)
      memory[0][i*banks_per_line+j].display(w, y0+1+i, x0+port_width+j*bank_width);
  }
  for (int j=0; j<final_banks; ++j)
    memory[0][full_lines*banks_per_line+j].display(w, y0+1+full_lines, x0+port_width+j*bank_width);
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

#endif
