#include <unistd.h>
#include <ncurses.h>
#include <pthread.h>

#include <map>
#include <vector>
#include <algorithm>

#include "options.h"
#include "caveat.h"
#include "cachesim.h"

const int VERDIV = 4;
const int HORDIV = 24;


option<bool>conf_view("view", false, true, "View running program");
option<long> conf_report("report", 100000000, "Status report frequency");

hart_t* mycpu;
uint64_t* counters;

//std::vector<long> topk;		// index into tcache, counters
extern std::map<long, const char*> fname; // dictionary of pc->name

std::map<long, Header_t*> blocks; // pc->bb
std::vector<Header_t*> topK;	  // bb's with highest counters

//bool cmpfunc(long& a, long& b) {
//  return counters[a] > counters[b];
bool cmpfunc(Header_t*& a, Header_t*& b) {
  return counters[index(a)] > counters[index(b)];
}

WINDOW *cores, *highest, *message, *code;
Header_t* first;	      // points to first visible basic block
int delta;		      // first visible instruction

void resize_windows(int verdiv =VERDIV, int hordiv =HORDIV)
{
  int lines, columns;
  getmaxyx(stdscr, lines, columns);
  cores = newwin(verdiv, hordiv, 0, 0);
  highest = newwin(lines-verdiv, columns-hordiv, verdiv, 0);
  message = newwin(verdiv, columns-hordiv, 0, hordiv);
  code = newwin(LINES-verdiv, columns-hordiv, verdiv, hordiv);
}

void paint_highest()
{
  int lines, columns;
  getmaxyx(highest, lines, columns);
  //  std::nth_element(topk.begin(), topk.begin()+lines, topk.end(), cmpfunc);
  //  std::sort(topk.begin(), topk.begin()+lines, cmpfunc);
  std::sort(topK.begin(), topK.end(), cmpfunc);
  wmove(highest, 0, 0);
  for (int j=0; j<lines; j++)
    //    wprintw(highest, "%12ld %8lx\n", counters[topk[j]], (long)bbp(&tcache[topk[j]])->addr);
    wprintw(highest, "%12ld %8lx\n", counters[index(topK[j])], (long)topK[j]->addr);
  wrefresh(highest);
  if (!first)
    first = topK[0];
}

void paint_code()
{
  int lines, columns;
  getmaxyx(highest, lines, columns);
  wclear(code);
  if (first) {
    std::map<long, Header_t*>::iterator iter = blocks.find(first->addr);
    Header_t* bb = iter->second;
    int count = 0;
    long pc = bb->addr;
    Insn_t* i;
    for (int k=0; k<lines; k++) {
      if (count == 0) {
	bb = iter->second;
	if (bb->addr != pc) {
	  wprintw(code, "%10s\n", bb->addr<pc ? "^^^" : "vvv");
	  pc = bb->addr;
	  continue;
	}
	i = insnp(bb+1);
	count = bb->count;
	iter++;
	//	wprintw(code, "%10s\n", "---");
	//	continue;
      }
      char buf[1024];
      int n = slabelpc(buf, pc);
      sdisasm(buf+n, pc, i);
      wprintw(code, "%10ld %10ld %8lx %s\n", counters[index(bb)], counters[i-tcache], pc, buf);
      pc += (i++)->compressed() ? 2 : 4;
      count--;
    }
  }
  wrefresh(code);
}

void* viewer(void* arg)
{
  initscr();
  raw();
  keypad(stdscr, TRUE);
  noecho();

  mouseinterval(0);	   /* no mouse clicks, just button up/down */
  // Don't mask any mouse events
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  nodelay(stdscr, TRUE);

  resize_windows();
  
  static long lines = 0;	// visible in display
  static Header_t* last = bbp((void*)tcache)+1;

  for (;;) {
    move(0, 0);
    usleep(1000000/10);
    /*
     * Add newly translated basic blocks
     */
    while (index(last) < tcache_size()) {
      Header_t* bb = last;
      blocks[(long)bb->addr] = bb;
      topK.push_back(last);
      Insn_t* i = insnp(bb) + bb->count;
      last += bb->count + 2;
      if (attributes[i->opcode()] & ATTR_cj)
	last++;
    }

  repaint_screen:
    paint_highest();
    paint_code();
    refresh();
    
    /*
     * Service mouse event
     */
    MEVENT event;
    switch (getch()) {
    case ERR:			// no key was pressed
      break;
    case 'q':
      endwin();
      exit(0);
      //    case REPORT_MOUSE_POSITION:
    case KEY_MOUSE:
      dieif(getmouse(&event)!=OK, "bad mouse event");
      if (event.bstate & BUTTON1_PRESSED) {
	int y = event.y, x = event.x;
	if (wmouse_trafo(highest, &y, &x, false)) {
	  first = topK[y];
	  delta = 0;
	}
      }
      else if (event.bstate & BUTTON4_PRESSED) {
	if (!first)
	  break;
	std::map<long, Header_t*>::iterator iter = blocks.find(first->addr);
	first = (--iter)->second;
      }
      else if (event.bstate & BUTTON5_PRESSED) {
	if (!first)
	  break;
	std::map<long, Header_t*>::iterator iter = blocks.find(first->addr);
	first = (++iter)->second;
      }
      goto repaint_screen;

    case KEY_RESIZE:
      resize_windows();
      goto repaint_screen;
    }
  }
}


void exitfunc()
{
  endwin();
  status_report();
  fprintf(stderr, "\n--------\n");
  for (hart_t* p=hart_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%ld] ", p->tid());
    p->print();
  }
  fprintf(stderr, "\n");
  status_report();
  fprintf(stderr, "\n");
}





int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "cachesim: RISC-V cache simulator");
  if (argc == 0)
    help_exit();
  
  mycpu = new hart_t(argc, argv, envp, true);
  counters = mycpu->counters();
  atexit(exitfunc);
  start_time();
  
  if (conf_view) {
    pthread_t viewer_thread;
    pthread_create(&viewer_thread, NULL, viewer, 0);
    mycpu->interpreter(view_simulator);
  }
  else
    mycpu->interpreter(dumb_simulator);
}
