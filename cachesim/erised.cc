#include <unistd.h>
#include <ncurses.h>

#include <map>
#include <vector>
#include <algorithm>

#include "options.h"
#include "caveat.h"
#include "cachesim.h"

const int VERDIV = 10;
const int HORDIV = 60;


extern option<long>conf_view;

hart_t* showing;

extern std::map<uintptr_t, const char*> fname; // dictionary of pc->name

struct bbfreq_t {
  uint64_t count;		// from counters
  uintptr_t addr;			// in case cache was flushed
  bbfreq_t(uint64_t c, uintptr_t a) { count=c; addr=a; }
};

static bool cmpfunc(bbfreq_t& a, bbfreq_t& b) {
  return a.count > b.count;
}

WINDOW *harts, *highest, *code;

static void resize_windows(int verdiv =VERDIV, int hordiv =HORDIV)
{
  int lines, columns;
  getmaxyx(stdscr, lines, columns);
  harts = newwin(verdiv, hordiv, 0, 0);
  highest = newwin(verdiv, columns-hordiv, 0, hordiv);
  code = newwin(lines-verdiv, columns-hordiv, verdiv, 0);
}


void* viewer_thread(void* arg)
{
  std::map<uintptr_t, const Header_t*> blocks;	  // pc->bb
  std::vector<bbfreq_t> topK;
  uintptr_t visible_pc = 0;
  
  char buf[1024];

  initscr();
  clear();
  noecho();
  cbreak();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  mouseinterval(0);	   /* no mouse clicks, just button up/down */
  MEVENT event;		// service mouse event
  
  //  raw();
  // Don't mask any mouse events
  //  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  //  mousemask(ALL_MOUSE_EVENTS, NULL);
  
  resize_windows();

  for (;;) {
    move(0, 0);
    usleep(1000000/conf_view());
    
    int ch;
    while ((ch = getch()) != ERR) {
      
      switch (ch) {
      case 'q':
	exit(0);

      case KEY_MOUSE:
	{
	  dieif(getmouse(&event)!=OK, "bad mouse event");
	  int y = event.y, x = event.x;
	  if (event.bstate & BUTTON1_PRESSED) {
	    if (wmouse_trafo(highest, &y, &x, false)) {
	      visible_pc = topK[y].addr;
	    }
	    else if (wmouse_trafo(harts, &y, &x, false)) {
	      int k = 0;
	      for (hart_t* h=hart_t::list(); h; h=h->next()) {
		if (k == y) {
		  showing = h;
		  visible_pc = 0;
		  break;
		}
		k++;
	      }
	    }
	    // else ignore click
	  }
	  if (event.bstate & (BUTTON4_PRESSED|BUTTON5_PRESSED)) { // scroll up/down
	    bool scrollup = (event.bstate & BUTTON4_PRESSED) != 0;
	    if (wmouse_trafo(code, &y, &x, false)) {
	      beep();
	      auto p = blocks.find(visible_pc);
	      if (p != blocks.end()) {
		if (scrollup)
		  p--;
		else
		  p++;
	      }
	      if (p == blocks.end())
		visible_pc = 0;
	      else
		visible_pc = p->first;
	    }
	    else if (wmouse_trafo(harts, &y, &x, false)) {
	    }
	    // else other panels cannot scroll
	  }
	}
	break;

      case KEY_RESIZE:
	resize_windows();
	break;
      }
    }

 paint_screen:
    // collect basic block address and execution frequency information
    blocks.clear();
    topK.clear();
    
    dieif(pthread_mutex_lock(&showing->tcache.lock), "mutex_lock failed");

    for (int idx=0; idx<showing->tcache.size(); ) {
      const Header_t* bb = showing->tcache.bbptr(idx);
      topK.push_back( bbfreq_t(showing->counters[idx], bb->addr) );
      blocks[bb->addr] = bb;
      idx += 1 + bb->count + (bb->conditional ? 2 : 1);
    }
    
    // paint visible code
    {
      int lines, columns;
      getmaxyx(code, lines, columns);
      wclear(code);
      wmove(code, 0, 0);
      auto p = blocks.find(visible_pc);
      if (p == blocks.end())
	visible_pc = 0;
      if (visible_pc) {
	const Header_t* bb = p->second;
	uintptr_t pc = bb->addr;
	const uint64_t* c = showing->counters.ptr(showing->tcache.index(bb));
	for (;;) {
	  const Insn_t* i = insnp(bb+1);
	  uint64_t bbcount = *c++;
	  for (int k=0; k<bb->count; k++) {
	    int n = slabelpc(buf, pc);
	    sdisasm(buf+n, pc, i);
	    if (attributes[i->opcode()] & (ATTR_ld|ATTR_st))
	      wprintw(code, "%10ld %6.2f%% %s\n", bbcount, 100.0*(*c)/bbcount, buf);
	    else
	      wprintw(code, "%10ld %7s %s\n", bbcount, "", buf);
	    //wprintw(code, "%s\n", buf);
	    pc += (i++)->compressed() ? 2 : 4;
	    c++;
	    //	    if (--lines == 0)
	    if (--lines == 2)
	      goto end_of_panel;
	  }
	  c += bb->conditional ? 2 : 1;
	  bb = bbp(i + (bb->conditional ? 2 : 1));
	}
      }
    end_of_panel:
      wrefresh(code);
    }

    dieif(pthread_mutex_unlock(&showing->tcache.lock), "mutex_unlock failed");
    
    // find most frequent K basic blocks
    std::sort(topK.begin(), topK.end(), cmpfunc);
    //  std::nth_element(topk.begin(), topk.begin()+K, topk.end(), cmpfunc);
    {
      int lines, columns;
      getmaxyx(highest, lines, columns);
      wclear(highest);
      wmove(highest, 0, 0);
      for (int j=0; j<lines && j<topK.size(); j++) {
	slabelpc(buf, topK[j].addr);
	wprintw(highest, "%12ld %s\n", topK[j].count, buf);
      }
      wrefresh(highest);
    }

    // paint harts
    {
      int lines, columns;
      getmaxyx(harts, lines, columns);
      wclear(harts);
      wmove(harts, 0, 0);
      double realtime = elapse_time();
      for (hart_t* h=hart_t::list(); h; h=h->next()) {
	long N = h->executed();
	wprintw(harts, "[%6d] %12ld insns %3.1f MIPS\n", h->tid(), N, N/1e6/realtime);
      }
      wrefresh(harts);
    }
    
    //    dieif(pthread_mutex_unlock(&showing->tcache.lock), "mutex_unlock failed");
  }
}
