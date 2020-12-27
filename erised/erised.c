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


struct perfCounters_t perf;

long func_begin, func_end;		/* PC range of interest */
int top =0;

time_t start_tick;
long insn_count =0;

inline static int min(int x, int y) { return x < y ? x : y; }

#define EPSILON  0.001

MEVENT event;
int button;
MEVENT what;


void paint_tv(WINDOW* win)
{
  long pc = func_begin;
  for (int i=0; i<top; i++)
    pc += shortOp(insn(pc)->op_code) ? 2 : 4;
  wmove(win, 0, 0);
  wprintw(win, "[%lx, %lx) ", func_begin, func_end);
  wprintw(win, "Mouse at row=%d, col=%d, bstate=0x%08lx, button=%d, what.bevent=0x%08lx, what.x=%d, what.y=%d\n",
	  event.y, event.x, event.bstate, button, what.bstate, what.x, what.y);
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
    int n = format_insn(buf, &c->i, pc);
    wprintw(win, "  %s", buf);
    int sz = shortOp(c->i.op_code) ? 1 : 2;
    pc += 2*sz;
    c  += sz, ibm += sz, icm += sz, dcm += sz;
  }
  wclrtobot(win);
  wrefresh(win);
}

#define FRAMERATE  (1.0/30)*1000

void interactive(WINDOW* win)
{
  struct timeval t1, t2;
  for (;;) {
    gettimeofday(&t1, 0);

    paint_tv(win);
    int ch = wgetch(win);
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
      if (event.bstate & BUTTON1_CLICKED)
	what = event;
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
  func_begin = perf.p.base;
  func_end = perf.p.bound;

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

  interactive(stdscr);
  
  printf("\033[?1003l\n"); // Disable mouse movement events, as l = low
  endwin();
  return 0;
}
