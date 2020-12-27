/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <curses.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "perfctr.h"


int menu_lines = 1;
int global_width = 25;
int local_width = 25;

struct perfCounters_t perf;

WINDOW *menu, *global, *local, *disasm;
int show_line;

long func_begin, func_end;		/* PC range of interest */
int top =0;

time_t start_tick;
long insn_count =0;

inline static int min(int x, int y) { return x < y ? x : y; }

#define EPSILON  0.001

MEVENT event;
int button;
MEVENT what;

struct count_bin_t {
  long count;
  long cycles;
};

void paint_histo(WINDOW* win, long base, long bound)
{
  werase(win);
  long rows, cols;
  getmaxyx(win, rows, cols);
  cols -= 2;
  struct count_bin_t* bin = (struct count_bin_t*)alloca(rows*sizeof(struct count_bin_t));
  const struct count_t* c = count(base);
  long n = ((bound-base)/2) / rows;
  long max_cycles = 0;
  for (int y=0; y<rows; y++) {
    long count=0, cycles=0;
    const struct count_t* limit = c + n;
    while (c < limit) {
      count += c->count;
      cycles += c->cycles;
      c += shortOp(c->i.op_code) ? 1 : 2;
    }
    bin[y].count = count;
    bin[y].cycles = cycles;
    if (cycles > max_cycles)
      max_cycles = cycles;
  }
  long cycles_per_bar = max_cycles / cols;
  for (int y=0; y<rows; y++) {
    wmove(win, y, 0);
    wprintw(win, "%12ld %12ld", bin[y].count, bin[y].cycles);
#if 0
    long x = cols - bin[y].cycles/cycles_per_bar;
    wmove(win, y, x);
    attron(A_REVERSE);
    while (x++ < cols)
      wprintw(win, "%c", '.');
    attroff(A_REVERSE);
    wprintw(win, "%2d", bin[y].cycles/cycles_per_bar);
#endif
  }
  wrefresh(win);
}

void paint_disasm(WINDOW* win)
{
  long pc = func_begin;
  for (int i=0; i<top; i++)
    pc += shortOp(insn(pc)->op_code) ? 2 : 4;
  wmove(win, 0, 0);
  const struct count_t* c = count(pc);
  const long* ibm = ibmiss(pc);
  const long* icm = icmiss(pc);
  const long* dcm = dcmiss(pc);
  for (int y=0; y<LINES-1 && pc<func_end; y++) {
    double cpi = (double)c->cycles/c->count;
    int dim = cpi < 1.0+EPSILON || c->count == 0;
    if (dim)  attron(A_DIM);
    wprintw(win, "%14ld  %6.3f", c->count, (double)c->cycles/c->count); 
    if (*ibm)  wprintw(win, "%6.3f", (double)*ibm/c->count);  else wprintw(win, "%6s", "");
    if (*icm)  wprintw(win, "%6.3f", (double)*icm/c->count);  else wprintw(win, "%6s", "");
    if (*dcm)  wprintw(win, "%6.3f", (double)*dcm/c->count);  else wprintw(win, "%6s", "");
    if (dim)  attroff(A_DIM);
    char buf[1024];
    int n = format_insn(buf, &c->i, pc, *image(pc));
    wprintw(win, "  %s", buf);
    int sz = shortOp(c->i.op_code) ? 1 : 2;
    pc += 2*sz;
    c  += sz, ibm += sz, icm += sz, dcm += sz;
  }
  wclrtobot(win);
  wrefresh(win);
}

#define FRAMERATE  (1.0/30)*1000

void interactive()
{
  struct timeval t1, t2;
  for (;;) {
    gettimeofday(&t1, 0);
    paint_histo(global, perf.h->base, perf.h->bound);
    long scale = (perf.h->bound - perf.h->base)/2 / LINES;
    paint_histo(local, perf.h->base+show_line*scale, perf.h->base+(show_line+1)*scale);
    paint_disasm(disasm);
    int ch = wgetch(stdscr);
    if (ch == ERR) {
      gettimeofday(&t2, 0);
      double msec = (t2.tv_sec - t1.tv_sec)*1000;
      msec += (t2.tv_usec - t1.tv_usec)/1000.0;
      usleep((FRAMERATE - msec) * 1000);
      continue;
    }
    
    button = ch;
    switch (ch) {
      //case KEY_F(1):
    case 'q':
      return;
    case KEY_DOWN:
      top++;
      break;
    case KEY_UP:
      if (top > 0)
	top--;
      break;
    case KEY_MOUSE:
      dieif(getmouse(&event) != OK, "Got bad mouse event.");
      if (wenclose(global, event.y, event.x))
	show_line = event.y;
      break;
    }
  }
}


long atohex(const char* p)
{
  for (long n=0; ; p++) {
    long digit;
    if ('0' <= *p && *p <= '9')
      digit = *p - '0';
    else if ('a' <= *p && *p <= 'f')
      digit = 10 + (*p - 'a');
    else if ('A' <= *p && *p <= 'F')
      digit = 10 + (*p - 'F');
    else
      return n;
    n = 16*n + digit;
  }
}


static const char* perf_path =0;
static int list =0;
  
const char* usage = "erised --count=name [erised-options]";
const struct options_t opt[] =
  {  { "--perf=s",	.s=&perf_path,	.ds=0,	.h="Shared memory counting structure =name" },
     { 0 }
  };


int main(int argc, const char** argv)
{
  int numopts = parse_options(argv+1);
  if (!perf_path)
    help_exit();
  perf_open(perf_path);
  func_begin = perf.h->base;
  func_end = perf.h->bound;

  initscr();			/* Start curses mode */
  start_color();		/* Start the color functionality */
  cbreak();			/* Line buffering disabled */
  noecho();
  nodelay(stdscr, TRUE);
  keypad(stdscr, TRUE);		/* Need all keys */
  // Don't mask any mouse events
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  printf("\033[?1003h\n"); // Makes the terminal report mouse movement events

  //  init_pair(1, COLOR_CYAN, COLOR_BLACK);

  //  menu   = newwin(1, COLS, 0, 0);
  global = newwin(LINES, global_width, 0, 0);
  local  = newwin(LINES, local_width,  0, global_width);
  disasm = newwin(LINES-1, COLS-global_width-local_width, menu_lines, global_width+local_width);

  interactive();
  
  printf("\033[?1003l\n"); // Disable mouse movement events, as l = low
  endwin();
  return 0;
}
