
const int port_width = 24;
const int bank_width = 16;
const int banks_per_line = 8;
const int memory_display_lines = (memory_banks+banks_per_line-1)/banks_per_line + 1;

extern bool wide_display;


inline void init_colors() {
  start_color();
  init_pair(6, COLOR_WHITE,	COLOR_BLACK); // default
  init_pair(1, COLOR_WHITE,	COLOR_BLACK); // next to dispatch
  init_pair(2, COLOR_MAGENTA,	COLOR_BLACK); // immediate execution
  init_pair(3, COLOR_RED,	COLOR_BLACK); // queued for issue
  init_pair(4, COLOR_GREEN,	COLOR_BLACK); // in execution
  init_pair(5, COLOR_WHITE,	COLOR_BLACK); // retired
}  

#if 0
#define color_default()		wattron(w, COLOR_PAIR(0) | A_NORMAL)
#define color_dispatch()	wattron(w, COLOR_PAIR(1) | A_BOLD)
#define color_immediate()	wattron(w, COLOR_PAIR(2) | A_BOLD)
#define color_queued()		wattron(w, COLOR_PAIR(3) | A_BOLD)
#define color_execute()		wattron(w, COLOR_PAIR(4) | A_BOLD)
#define color_retired()		wattron(w, COLOR_PAIR(5) | A_DIM)

#else

#define color_default()		wattron(w, COLOR_PAIR(6))
#define color_dispatch()	wattron(w, COLOR_PAIR(1))
#define color_immediate()	wattron(w, COLOR_PAIR(2))
#define color_queued()		wattron(w, COLOR_PAIR(3))
#define color_execute()		wattron(w, COLOR_PAIR(4))
#define color_retired()		wattron(w, COLOR_PAIR(5))

#endif

