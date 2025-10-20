#include <cassert>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>

#include <ncurses.h>

#include "caveat.h"
#include "hart.h"

#include "core.h"
#include "memory.h"

option<long> conf_report("report", 1, "Status report per second");
option<bool> conf_visual("visual", true, false, "Interactive visual mode");

#if 0
option<int> conf_fp("fp", 3, "Latency floating point");
option<int> conf_ld("ld", 4, "Latency loads");
option<int> conf_st("st", 20, "Latency stores");
option<int> conf_alu("alu", 1, "Latency ALU");
#else
option<int> conf_fp("fp", 1, "Latency floating point");
option<int> conf_ld("ld", 1, "Latency loads");
option<int> conf_st("st", 1, "Latency stores");
option<int> conf_alu("alu", 1, "Latency ALU");
#endif

uint8_t latency[Number_of_Opcodes];


void status_report()
{
  double realtime = elapse_time();
  fprintf(stderr, "\r\33[2K%12llud insns %3.1fs CPS=%3.1f IPC=", cycle, realtime, cycle/1e6/realtime);

  if (hart_t::num_harts() <= 16) {
    char separator = '(';
    for (Core_t* p=Core_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c", separator);
      fprintf(stderr, "%5.3f", (double)p->executed()/cycle);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::num_harts() > 1)
    fprintf(stderr, "(%d cores)", hart_t::num_harts());
}

void* status_thread(void* arg)
{
  while (1) {
    usleep(1000000/conf_report());
    status_report();
  }
}

void exitfunc()
{
  fprintf(stderr, "\nNormal exit\n");
  status_report();
  fprintf(stderr, "\n");
}






const int window_buffers = 100;
WINDOW* winbuf[window_buffers];
int frontwin = 0;
int behind = 0;

void interactive(Core_t* cpu)
{
  initscr();                    /* Start curses mode */
  keypad(stdscr, true);         /* Need all keys */
  nonl();
  cbreak();                     /* Line buffering disabled */
  noecho();
  nodelay(stdscr, true);
  //start_color();

  for (int k=0; k<window_buffers; ++k)
    winbuf[k] = newwin(LINES, COLS, 0, 0);
  
  long framerate = 20000;
  long long stop_cycle = cycle;
  long long number = 0;
  int ch;
  
 infinite_loop:
  while ((ch=getch()) == ERR) {
    if (cycle < stop_cycle) {
#ifdef VERIFY
      History_t* h = cpu->nextrob();
      clock_memory_system();
      if (cpu->clock_pipeline()) {
	cpu->single_step();
	if (h->ref->rd() != NOREG)
	  h->expected_rd = cpu->get_rd_from_spike(h->ref->rd());
      }
#else
      clock_memory_system();
      (void) cpu->clock_pipeline();
#endif

      frontwin=(frontwin+1)%window_buffers;
      WINDOW* w=winbuf[frontwin];
      wclear(w);
      display_memory(w, 0, 0);
      display_history(w, 2, 0, cpu, LINES-3);
      wrefresh(w);
      if (framerate)
	usleep(framerate);
    }
  }
  framerate = 20000;

  switch (ch) {
  case 'q':
    endwin();
    return;
#if 0
  case 'g':
    fastrun_without_display(this);
    return;
#endif
  case 'h':
    extern void help_screen();
    help_screen();
    while ((ch=getch()) == ERR)
      usleep(100000);
    wrefresh(winbuf[frontwin]);
    goto infinite_loop;
    
  case '0'...'9':
    number = 10*number + ch-'0';
    goto infinite_loop;

    
  case 'b':
    if (behind < window_buffers) {
      ++behind;
      overwrite(winbuf[ (frontwin-behind+window_buffers) % window_buffers ], stdscr);
      refresh();
    }
    goto infinite_loop;
  case 'f':
    if (behind > 0) {
      --behind;
      overwrite(winbuf[ (frontwin-behind+window_buffers) % window_buffers ], stdscr);
      refresh();
    }
    else {			// single step one cycle
      stop_cycle = cycle + 1;
      number = 0;
    }
    goto infinite_loop;
    
  case 'c':
    stop_cycle = number ? number : LLONG_MAX;
    number = 0;
    behind = 0;
    goto infinite_loop;
  case 'C':
    stop_cycle = number ? number : LLONG_MAX;
    number = 0;
    behind = 0;
    framerate = 0;
    goto infinite_loop;
  }
  stop_cycle = cycle;
  goto infinite_loop;
}



int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "nsosim: RISC-V non-speculative out-of-order simulator");
  if (argc == 0)
    help_exit();
#if 0
  if (conf_trace())
    trace_file = fopen(conf_trace(), "w");
#endif

  for (int k=0; k<Number_of_Opcodes; ++k) {
    Opcode_t op = (Opcode_t)k;
    ATTR_bv_t a = attributes[op];
    if      (a & ATTR_fp) latency[op] = conf_fp();
    else if (a & ATTR_ld) latency[op] = conf_ld();
    else if (a & ATTR_st) latency[op] = conf_st();
    else                  latency[op] = conf_alu();
  }
  
  Core_t* cpu = new Core_t(argc, argv, envp);
  cpu->clone = clone_proxy;
  cpu->riscv_syscall = ooo_riscv_syscall;
  atexit(exitfunc);

  if (conf_visual()) {
    start_time();
    cpu->reset();
    interactive(cpu);
  }
  else {
    if (conf_report() > 0 && !conf_show()) {
      pthread_t tnum;
      dieif(pthread_create(&tnum, 0, status_thread, 0), "failed to launch status_report thread");
    }
    start_time();
    cpu->reset();
    for (;;) {
#ifdef VERIFY
      History_t* h = cpu->nextrob();
      clock_memory_system();
      if (cpu->clock_pipeline()) {
	cpu->single_step();
	if (h->ref->rd() != NOREG)
	  h->expected_rd = cpu->get_rd_from_spike(h->ref->rd());
      }
#else
      clock_memory_system();
      (void) cpu->clock_pipeline();
#endif
    }
  }
}
