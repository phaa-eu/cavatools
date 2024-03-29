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

//#define DEBUG

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "cache.h"
#include "core.h"


#define FRAMERATE    60		/* frames per second */
#define PERSISTENCE  30		/* frames per color decay */
#define HOT_COLOR  (7-2)

#define COUNT_WIDTH  12
int global_width = COUNT_WIDTH;
int local_width = COUNT_WIDTH;

perf_t perf;
int corenum;

WINDOW *menu;
struct histogram_t global, local;
struct disasm_t disasm;
WINDOW* summary;

long insn_count =0;

inline static int min(int x, int y) { return x < y ? x : y; }

#define EPSILON  0.001

MEVENT event;


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

struct disasm_t {
  WINDOW* win;
  long* old;
  int* decay;
  long base, bound;
  long max_value;
  int lines;
};


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

void histo_compute(int corenum, struct histogram_t* histo, long base, long bound)
{
  if (base == 0 || bound == 0)
    return;
  long range = (bound-base) / histo->bins; /* pc range per bin */
  histo->base = base;
  histo->bound = bound;
  histo->range = range;
  long pc = base;
  struct insn_t* p = insn(pc);
  struct count_t* c = &perf.count[corenum][(pc-perf.h->base)/2];
  long max_count = 0;
  for (int i=0; i<histo->bins; i++) {
    long mcount = 0;
    long limit = pc + range;
    while (pc < limit) {
      //      mcount = max(mcount, c->count);
      mcount += c->count[0] + c->count[1] + c->count[2];
      if (shortOp(p->op_code))
	pc += 2, p += 1, c += 1;
      else
	pc += 4, p += 2, c += 2;
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
  if (count > 0 || always)
    wprintw(win, "%*ld", width, count);
  else
    wprintw(win, "%*s", width, "");
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



void disasm_create(struct disasm_t* disasm, int lines, int cols, int starty, int startx)
{
  disasm->win = newwin(lines, cols, starty, startx);
  disasm->lines = lines;
  disasm->old = (long*)malloc(lines*sizeof(long));
  memset(disasm->old, 0, lines*sizeof(long));
  disasm->decay = (int*)malloc(lines*sizeof(int));
  memset(disasm->decay, 0, lines*sizeof(int));
}

void disasm_delete(struct disasm_t* disasm)
{
  if (disasm->old)
    free(disasm->old);
  if (disasm->decay)
    free(disasm->decay);
  memset(disasm, 0, sizeof(struct disasm_t));
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

void disasm_paint(int corenum, struct disasm_t* disasm)
{
  long numcycles = 0;
  WINDOW* win = disasm->win;
  long pc = disasm->base;
  core_t* cpu = &perf.core[corenum];
  struct count_t* countA = perf.count[corenum];
  long* icmissA = perf.icmiss[corenum];
  long* dcmissA = perf.dcmiss[corenum];
#define pcount(pc)  &countA[(pc-perf.h->base)/2]
#define icmiss(pc) &icmissA[(pc-perf.h->base)/2]
#define dcmiss(pc) &dcmissA[(pc-perf.h->base)/2]
  wmove(win, 0, 0);
  wprintw(win, "%16s %-5s %-4s %-5s %-5s", "Count", " CPI", "#ssi", "I$", "D$");
  wprintw(win, "%7.1fB insns  CPI=%5.2f", cpu->count.insn/1e9, (double)cpu->count.cycle/cpu->count.insn);
  //  wprintw(win, "] %8s %8s %s\n", "PC", "Hex", "Assembly                q=quit");
  if (pc != 0) {
    const struct insn_t* p = insn(pc);
    struct count_t* c = pcount(pc);
    const long* icm = icmiss(pc);
    const long* dcm = dcmiss(pc);
    for (int y=1; y<getmaxy(win) && pc<perf.h->bound; y++) {    
      long total = c->count[0] + c->count[1] + c->count[2];
      
      wmove(win, y, 0);
      if (total != disasm->old[y])
	disasm->decay[y] = HOT_COLOR*PERSISTENCE;
      disasm->old[y] = total;
      paint_count_color(win, 16, total, disasm->decay[y], 1);
      if (disasm->decay[y] > 0)
	disasm->decay[y]--;
      double cpi = (double)c->cycles/total;
      int dim = cpi < 1.0+EPSILON || total == 0;

      /* average superscalar bundle size */
      long npc = pc;
      struct count_t* d = c;
      long bundle = d->count[0];
      //      wprintw(win, "%7ld %7ld %7ld", d->count[0], d->count[1], d->count[2]);
      for (int i=1; i<3; i++) {
	d    += (shortOp(insn(npc)->op_code) ? 1 : 2);
	npc  += (shortOp(insn(npc)->op_code) ? 2 : 4);
	bundle += d->count[i];
      }
      double assb = (double)bundle/total;
      if (dim)  wattron(win, A_DIM);
      if (total == 0 || cpi < 0.01) wprintw(win, " %-5s", "");
      else if (c->cycles == total)  wprintw(win, " %-5s", " 1");
      else                          wprintw(win, " %5.2f", cpi);
      if (total == 0 || assb < 0.01)
	wprintw(win, "     ");
      //    else if (total == bundle)
      else if (0.99 < assb && assb < 1.01)
	wprintw(win, " 1   ");
      else
	wprintw(win, " %4.2f", assb);
      char buf[1024];
      char* b = buf;
      b+=fmtpercent(b, *icm, total);
      b+=fmtpercent(b, *dcm, total);
      //      b+=sprintf(b , " %8ld", *icm);
      //      b+=sprintf(b , " %8ld", *dcm);
      b+=sprintf(b, " ");
      b+=format_pc(b, 28, pc);
      b+=format_insn(b, p, pc, *((unsigned int*)pc));
      wprintw(win, "%s\n", buf);
      if (dim)  wattroff(win, A_DIM);
      if (shortOp(p->op_code))
	pc += 2, p += 1, c += 1, icm += 1, dcm += 1;
      else
	pc += 4, p += 2, c += 2, icm += 2, dcm += 2;
    }
  }
  disasm->bound = pc;
  wclrtobot(win);
  wnoutrefresh(win);
}

void paint_summary()
{
  wclrtobot(summary);
  for (int i=0; i<perf.h->active; i++) {
    wmove(summary, i, 0);
    core_t* cpu = perf.core + i;
    double ipc = (double)cpu->count.insn / cpu->count.cycle;
    double pcti = cpu->count.insn / 100.0;
    if (i == corenum)
      wattron(summary, A_REVERSE);
    wprintw(summary,"  Core[%ld]%c %d IPC=%4.2f I$=%6.3f%% D$=%6.3f%%(%ld) insn=%14ld(%5ld ecalls) cycle=%14ld\n",
	    i, cpu->running?'#':' ', cpu->tid, ipc, cpu->icache.misses/pcti, cpu->dcache.misses/pcti, cpu->dcache.misses, cpu->count.insn, cpu->count.ecalls, cpu->count.cycle, ipc);
    if (i == corenum)
      wattroff(summary, A_REVERSE);
  }
  wnoutrefresh(summary);
}

void resize_histos()
{
  histo_delete(&global);
  histo_delete(&local);
  disasm_delete(&disasm);
  histo_create(&global, LINES, global_width, 0, 0);
  histo_compute(corenum, &global, perf.h->base, perf.h->bound);
  histo_create(&local, LINES, local_width, 0, global_width);
  histo_compute(corenum, &local, 0, 0);
  summary = newwin(perf.h->cores, COLS-global_width-local_width, 0, global_width+local_width);
  disasm_create(&disasm, LINES-perf.h->cores, COLS-global_width-local_width, perf.h->cores, global_width+local_width);
  disasm.base = 0;
  doupdate();
}

void interactive()
{
  struct timeval t1, t2;
  for (;;) {
    gettimeofday(&t1, 0);
    //   histo_compute(&global, perf.h->base, perf.h->bound);
    histo_compute(corenum, &global, perf.h->base, perf.h->bound);
    histo_compute(corenum, &local, local.base, local.bound);
    paint_summary();
    disasm_paint(corenum, &disasm);
    histo_paint(&global, "Global", local.base, local.bound);
    histo_paint(&local, "Local", disasm.base, disasm.bound);
    doupdate();
    int ch = wgetch(stdscr);
    switch (ch) {
    case ERR:
      gettimeofday(&t2, 0);
      double msec = (t2.tv_sec - t1.tv_sec)*1000;
      msec += (t2.tv_usec - t1.tv_usec)/1000.0;
      usleep((1000/FRAMERATE - msec) * 1000);
      break;
      //case KEY_F(1):
#if 0
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
      if (wenclose(disasm.win, event.y, event.x)) {
	if (event.bstate & BUTTON4_PRESSED) {
	  if (disasm.base > perf.h->base) {
	    disasm.base -= 2;
	    if (insn(disasm.base)->op_code == Op_zero)
	      disasm.base -= 2;
	  }
	}
	else if (event.bstate & BUTTON5_PRESSED) {
	  long npc = disasm.base + (shortOp(insn(disasm.base)->op_code) ? 2 : 4);
	  if (npc < perf.h->bound)
	    disasm.base = npc;
	}
      }
      else if (wenclose(summary, event.y, event.x)) {
	if (event.bstate & BUTTON1_PRESSED) {
	  corenum = event.y;
	}
      }
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
	  disasm.base = local.base + (event.y-1)*local.range;
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
	
	if (local.base < perf.h->base)
	  local.base = perf.h->base;
	if (local.bound > perf.h->bound)
	  local.bound = perf.h->bound;
	local.range = (local.bound-local.base)/local.bins;
      }
      //break;
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
  {  { "--perf=s",	.s=&perf_path,	.ds="caveat",	.h="Shared memory counting structure =name" },
     { 0 }
  };


int main(int argc, const char** argv)
{
  int numopts = parse_options(argv+1);
  if (argc == numopts+1 || !perf_path)
    help_exit();
  long entry = load_elf_binary(argv[1+numopts], 0);
  perf_init(perf_path, 0);

  initscr();			/* Start curses mode */
  start_color();		/* Start the color functionality */
  cbreak();			/* Line buffering disabled */
  noecho();
  keypad(stdscr, TRUE);		/* Need all keys */
  mouseinterval(0);	   /* no mouse clicks, just button up/down */
  // Don't mask any mouse events
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  //mousemask(ALL_MOUSE_EVENTS, NULL);
  
  nodelay(stdscr, TRUE);
  //timeout(100);

  if (has_colors() == FALSE) {
    endwin();
    puts("Your terminal does not support color");
    exit(1);
  }
  start_color();
  init_pair(2, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(3, COLOR_BLUE,    COLOR_BLACK);
  init_pair(4, COLOR_CYAN,    COLOR_BLACK);
  init_pair(5, COLOR_GREEN,   COLOR_BLACK);
  init_pair(6, COLOR_YELLOW,  COLOR_BLACK);
  init_pair(7, COLOR_RED,     COLOR_BLACK);

  resize_histos();
  interactive();
  
  //  printf("\033[?1003l\n"); // Disable mouse movement events, as l = low
  endwin();
  perf_close();
  return 0;
}
