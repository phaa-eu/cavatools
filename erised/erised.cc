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
#include <ncurses.h>

//#define DEBUG

#include "../uspike/options.h"
#include "../uspike/uspike.h"
#include "../uspike/instructions.h"
#include "../caveat/perf.h"


#define FRAMERATE    30		/* frames per second */
#define PERSISTENCE  30		/* frames per color decay */
#define HOT_COLOR  (7-2)

#define COUNT_WIDTH  12
int global_width = COUNT_WIDTH;
int local_width = COUNT_WIDTH;

/*  Histogram functions.  */

struct histogram_t {
  WINDOW* win;
  long* bin;
  int* decay;
  long base, bound;
  long range;
  long max_value;
  int bins;
};

struct assembly_t {
  WINDOW* win;
  long* old;
  int* decay;
  long base, bound;
  long max_value;
  int lines;
};

perf_t** perf;
perf_t* cur_core;

WINDOW *menu;
struct histogram_t global, local;
struct assembly_t assembly;
//WINDOW* summary;

long insn_count =0;

inline static int min(int x, int y) { return x < y ? x : y; }

#define EPSILON  0.001

MEVENT event;



#define max(a, b)  ( (a) > (b) ? (a) : (b) )

void histo_create(struct histogram_t* histo, int lines, int cols, int starty, int startx)
{
  histo->win = newwin(lines, cols, starty, startx);
  histo->bins = --lines;
  histo->bin = (long*)malloc(lines*sizeof(long));
  memset(histo->bin, 0, lines*sizeof(long));
  histo->decay = (int*)malloc(lines*sizeof(int));
  memset(histo->decay, 0, lines*sizeof(int));
};

void histo_delete(struct histogram_t* histo)
{
  if (histo->bin)
    free(histo->bin);
  if (histo->decay)
    free(histo->decay);
  memset(histo, 0, sizeof(struct histogram_t));
}

void histo_compute(perf_t* p, struct histogram_t* histo, long base, long bound)
{
  if (base == 0 || bound == 0)
    return;
  long range = (bound-base) / histo->bins; /* pc range per bin */
  histo->base = base;
  histo->bound = bound;
  histo->range = range;
  long pc = base;
  long max_count = 0;
  for (int i=0; i<histo->bins; i++) {
    long mcount = 0;
    long end = pc + range;
    if (end > bound)
      end = bound;
    while (pc < end) {
      mcount += p->count(pc);
      pc += code.at(pc).compressed() ? 2 : 4;
    }
    if (mcount != histo->bin[i])
      histo->decay[i] = HOT_COLOR*PERSISTENCE;
    histo->bin[i] = mcount;
    max_count = max(max_count, mcount);
  }
  histo->max_value = max_count;
}

void paint_count_color(WINDOW* win, int width, long count, int decay, int always)
{
  int heat = (decay + PERSISTENCE-1)/PERSISTENCE;
  wattron(win, COLOR_PAIR(heat + 2));
  //  if (count > 0 || always)
    wprintw(win, "%*ld", width, count);
    //  else
    //    wprintw(win, "%*s", width, "");
  wattroff(win, COLOR_PAIR(heat + 2));
}

void histo_paint(struct histogram_t* histo, const char* title, long base, long bound)
{
  werase(histo->win);
  long rows, cols;
  getmaxyx(histo->win, rows, cols);
  rows--;
  wmove(histo->win, 0, 0);
  wprintw(histo->win, "%*s\n", cols-1, title);
  long pc = histo->base;
  for (int y=0; y<rows; y++) {
    int highlight = (base <= pc && pc < bound || pc <= base && bound < pc+histo->range);
    if (highlight)  wattron(histo->win, A_REVERSE);
    paint_count_color(histo->win, cols-1, histo->bin[y], histo->decay[y], 0);
    wprintw(histo->win, "\n");
    if (highlight)  wattroff(histo->win, A_REVERSE);
    if (histo->decay[y] > 0)
      histo->decay[y]--;
    pc += histo->range;
  }
  wnoutrefresh(histo->win);
}



void assembly_create(struct assembly_t* assembly, int lines, int cols, int starty, int startx)
{
  assembly->win = newwin(lines, cols, starty, startx);
  assembly->lines = lines;
  assembly->old = (long*)malloc(lines*sizeof(long));
  memset(assembly->old, 0, lines*sizeof(long));
  assembly->decay = (int*)malloc(lines*sizeof(int));
  memset(assembly->decay, 0, lines*sizeof(int));
}

void assembly_delete(struct assembly_t* assembly)
{
  if (assembly->old)
    free(assembly->old);
  if (assembly->decay)
    free(assembly->decay);
  memset(assembly, 0, sizeof(struct assembly_t));
}   

//inline int fmtpercent(char* b, long num, long over)
int fmtpercent(char* b, long num, long over)
{
  if (num == 0)         return sprintf(b, " %4s ", "");
  double percent = 100.0 * num / over;
  if (percent > 99.9) return sprintf(b, " %4d%%", (int)percent);
  if (percent > 9.99) return sprintf(b, " %4.1f%%", percent);
  if (percent > 0.99) return sprintf(b, " %4.2f%%", percent);
  return sprintf(b, " .%03u%%", (unsigned)((percent+0.005)*100));
}

void assembly_paint(perf_t* p, struct assembly_t* assembly)
{
  long numcycles = 0;
  WINDOW* win = assembly->win;
  long pc = assembly->base;
  wmove(win, 0, 0);
  wprintw(win, "%16s %-5s %-4s %-5s %-5s", "Count", " CPI", "#ssi", "I$", "D$");
  wprintw(win, "] %8s %8s %s\n", "PC", "Hex", "Assembly                q=quit");
  if (pc != 0) {
    for (int y=1; y<getmaxy(win) && pc<code.limit(); y++) {
      wmove(win, y, 0);
      if (p->count(pc) != assembly->old[y])
	assembly->decay[y] = HOT_COLOR*PERSISTENCE;
      assembly->old[y] = p->count(pc);
      paint_count_color(win, 16, p->count(pc), assembly->decay[y], 1);
      if (assembly->decay[y] > 0)
	assembly->decay[y]--;
      double cpi = (double)p->cycle(pc)/p->count(pc);
      int dim = cpi < 1.0+EPSILON || p->count(pc) == 0;
      if (dim)  wattron(win, A_DIM);
      /*
      if (p->count(pc) == 0 || cpi < 0.01) wprintw(win, " %-5s", "");
      else if (p->cycle(pc) == p->count(pc))  wprintw(win, " %-5s", " 1");
      else */                                    wprintw(win, " %5.2f", cpi);
      char buf[1024];
      char* b = buf;
      b+=fmtpercent(b, p->imiss(pc), p->count(pc));
      b+=fmtpercent(b, p->dmiss(pc), p->count(pc));
      //      b+=sprintf(b , " %8ld", *icm);
      //      b+=sprintf(b , " %8ld", *dcm);
      b+=sprintf(b, " ");
      b+=slabelpc(b,pc);
      b+=sdisasm(b, pc);
      wprintw(win, "%s\n", buf);
      if (dim)  wattroff(win, A_DIM);
      pc += code.at(pc).compressed() ? 2 : 4;
    }
  }
  assembly->bound = pc;
  wclrtobot(win);
  wnoutrefresh(win);
}

void resize_histos()
{
  histo_delete(&global);
  histo_delete(&local);
  assembly_delete(&assembly);
  histo_create(&global, LINES, global_width, 0, 0);
  histo_compute(cur_core, &global, code.base(), code.limit());
  histo_create(&local, LINES, local_width, 0, global_width);
  histo_compute(cur_core, &local, code.base(), code.limit());
  assembly_create(&assembly, LINES, COLS-global_width-local_width, 0, global_width+local_width);
  assembly.base = 0;
  doupdate();
}

void interactive()
{
  struct timeval t1, t2;
  for (;;) {
    gettimeofday(&t1, 0);
    histo_compute(cur_core, &global, code.base(), code.limit());
    histo_compute(cur_core, &local, local.base, local.bound);
    histo_paint(&global, "Global", local.base, local.bound);
    histo_paint(&local, "Local", assembly.base, assembly.bound);
    assembly_paint(cur_core, &assembly);
    doupdate();
    //    switch (wgetch(stdscr)) {
    switch (getch()) {
    case ERR:
      {
	gettimeofday(&t2, 0);
	double msec = (t2.tv_sec - t1.tv_sec)*1000.0;
	msec += (t2.tv_usec - t1.tv_usec)/1000.0;
	if (msec < 1000.0/FRAMERATE)
	  usleep((1000.0/FRAMERATE - msec) * 1000.0);
      }
      break;
#if 0
      //case KEY_F(1):
    case KEY_RESIZE:
      resizeterm(LINES, COLS);
      resize_histos();
      break;
#endif
    case 'q':
      return;
      //case KEY_DOWN:
      //case KEY_UP:
    case KEY_MOUSE:
      dieif(getmouse(&event) != OK, "Got bad mouse event.");
      if (wenclose(assembly.win, event.y, event.x)) {
	if (event.bstate & BUTTON4_PRESSED) {
	  assembly.base -= code.at(assembly.base-2).opcode() != Op_ZERO ? 2 : 4;
	  if (assembly.base < code.base())
	    assembly.base = code.base();
	  assembly.bound -= code.at(assembly.bound-2).opcode() != Op_ZERO ? 2 : 4;
	}
	else if (event.bstate & BUTTON5_PRESSED) {
	  assembly.bound += code.at(assembly.bound).compressed() ? 2 : 4;
	  if (assembly.bound > code.limit())
	    assembly.bound = code.limit();
	  assembly.base += code.at(assembly.base).compressed() ? 2 : 4;
	}
      }
      /*
      else if (wenclose(summary, event.y, event.x)) {
	if (event.bstate & BUTTON1_PRESSED) {
	  corenum = event.y;
	}
      }
      */
      else if (wenclose(global.win, event.y, event.x)) {
	if (event.bstate & BUTTON1_PRESSED) {
	  local.base = global.base + (event.y-1)*global.range;
	  local.bound = local.base + global.range;
	}
      }
      else if (wenclose(local.win, event.y, event.x)) {
	//int scroll = local.range * local.bins/2;
	int scroll = local.range;
	if (event.bstate & BUTTON1_PRESSED) {
	  assembly.base = local.base + (event.y-1)*local.range;
	}
	else if ((event.bstate & BUTTON4_PRESSED) && (event.bstate & BUTTON_SHIFT)) {
	  local.base  += scroll;
	  local.bound -= scroll;
	}
	else if ((event.bstate & BUTTON5_PRESSED) && (event.bstate & BUTTON_SHIFT)) {
	  local.base  -= scroll;
	  local.bound += scroll;
	}
	else if (event.bstate & BUTTON4_PRESSED) { /* without shift */
	  local.base  -= scroll;
	  local.bound -= scroll;
	}
	else if (event.bstate & BUTTON5_PRESSED) { /* without shift */
	  local.base  += scroll;
	  local.bound += scroll;
	}
      }
      break;
    } // switch
    if (local.base < code.base())
      local.base = code.base();
    if (local.bound > code.limit())
      local.bound = code.limit();
    local.range = (local.bound-local.base)/local.bins;
    if (assembly.base < code.base())
      assembly.base = code.base();
    if (assembly.bound > code.limit())
      assembly.bound = code.limit();
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

int main(int argc, const char** argv)
{
  parse_options(argc, argv, "erised: real-time viewer for caveat");
  if (argc == 0)
    help_exit();
  code.loadelf(argv[0]);
  perf_t::open("caveat");
  perf = new perf_t*[perf_t::cores()];
  for (int i=0; i<perf_t::cores(); i++) {
    perf[i] = new perf_t(i);
  }
  cur_core = perf[0];

  initscr();			/* Start curses mode */
  keypad(stdscr, true);		/* Need all keys */
  nonl();
  cbreak();			/* Line buffering disabled */
  noecho();

  start_color();		/* Start the color functionality */
  init_pair(2, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(3, COLOR_BLUE,    COLOR_BLACK);
  init_pair(4, COLOR_CYAN,    COLOR_BLACK);
  init_pair(5, COLOR_GREEN,   COLOR_BLACK);
  init_pair(6, COLOR_YELLOW,  COLOR_BLACK);
  init_pair(7, COLOR_RED,     COLOR_BLACK);
  
  mouseinterval(0);	   /* no mouse clicks, just button up/down */
  // Don't mask any mouse events
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  //mousemask(ALL_MOUSE_EVENTS, NULL);
  
  nodelay(stdscr, TRUE);
  //timeout(100);

  resize_histos();
  interactive();
  
  //  printf("\033[?1003l\n"); // Disable mouse movement events, as l = low
  endwin();
  return 0;
}
