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

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"

#include "../pipesim/pipesim.h"


struct line_t {
  long addr;
  long regv;
  long cycle;
  int delta;
  int rd;
};

struct countSpace_t countSpace;

long func_begin, func_end;		/* PC range of interest */
struct line_t* lines;		/* indexed by (pc-func_begin)/2 */
int top =0;

time_t start_tick;
long insn_count =0;

struct fifo_t* trace_buffer;
int hart;
uint64_t memq[tr_memq_len];
long timeq[tr_memq_len];


inline static int min(int x, int y) { return x < y ? x : y; }

#define EPSILON  0.001

void paint_tv(WINDOW* win)
{
  long pc = func_begin;
  struct count_t* c = count(pc);
  for (int i=0; i<top; i++) {
    pc += shortOp(c->i.op_code) ? 2 : 4;
    c  += shortOp(c->i.op_code) ? 1 : 2;
  }
  wmove(win, 0, 0);
  for (int y=0; y<LINES && pc<func_end; y++) {
    double cpi = (double)c->cycles/c->count;
    if (cpi < 1.0+EPSILON || c->count == 0) {
      attron(A_DIM);
      wprintw(win, "%14ld  %6.3f", c->count, (double)c->cycles/c->count);
      attroff(A_DIM);
    }
    else
      wprintw(win, "%14ld  %6.3f", c->count, (double)c->cycles/c->count);
    char buf[1024];
    int n = format_insn(buf, &c->i, pc);
    wprintw(win, "%s", buf);
    pc += shortOp(c->i.op_code) ? 2 : 4;
    c  += shortOp(c->i.op_code) ? 1 : 2;
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
    if (ch != ERR) {
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
      }
    }

    gettimeofday(&t2, 0);
    double msec = (t2.tv_sec - t1.tv_sec)*1000;
    msec += (t2.tv_usec - t1.tv_usec)/1000.0;
    usleep((FRAMERATE - msec) * 1000);
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


int main(int argc, const char** argv)
{
  static const char* count_path =0;
  static const char* func_name =0;
  static int list =0;
  
  static struct options_t opt[] =
    {  { "--count=",	.v=&count_path,	.h="Shared memory counting structure" },
       { "--func=",	.v=&func_name,	.h="Function to examine" },
       { 0 }
    };
  int numopts = parse_options(opt, argv+1,
			      "erised --count=name [erised-options]"
			      "\n\t- real-time view of pipesim");
  if (argc < 2)
    help_exit();
  long entry =0;
  if (argc > numopts+1) {
    entry = load_elf_binary(argv[1+numopts], 0);
    countSpace_init(count_path, 1);
  }
  if (!func_name)
    func_name = "_start";
  if (! find_symbol(func_name, &func_begin, &func_end)) {
    fprintf(stderr, "function %s cannot be found in symbol table\n", func_name);
    exit(1);
  }

  initscr();			/* Start curses mode */
  start_color();		/* Start the color functionality */
  cbreak();			/* Line buffering disabled */
  keypad(stdscr, TRUE);		/* Need all keys */
  noecho();
  init_pair(1, COLOR_CYAN, COLOR_BLACK);
  nodelay(stdscr, TRUE);

  interactive(stdscr);
  endwin();
  
  return 0;
}



void countSpace_init(const char* shm_name, int reader)
{
  countSpace.base  = insnSpace.base;
  countSpace.bound = insnSpace.bound;
  size_t size = sizeof(struct count_t) * ((countSpace.bound - countSpace.base) / 2);
  if (!shm_name)
    countSpace.insn_array = (struct count_t*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  else if (!reader) {
    int fd = shm_open(shm_name, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
    dieif(fd<0, "shm_open() failed in countSpace_init");
    dieif(ftruncate(fd, size)<0, "ftruncate() failed in countSpace_init");
    countSpace.insn_array = (struct count_t*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  }
  else {
    int fd = shm_open(shm_name, O_RDONLY, 0);
    dieif(fd<0, "shm_open() failed in countSpace_init");
    countSpace.insn_array = (struct count_t*)mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
  }
  fprintf(stderr, "Opening %s, reader=%d\n", shm_name, reader);
  dieif(countSpace.insn_array==0, "mmap() failed in countSpace_init");
  if (!shm_name || !reader) {
    memset((char*)countSpace.insn_array, 0, size);
    for (Addr_t pc=countSpace.base; pc<countSpace.bound; pc+=2)
      decode_instruction(&countSpace.insn_array[(pc-countSpace.base)/2].i, pc);
  }
}
