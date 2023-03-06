#include <unistd.h>
#include <ncurses.h>
#include <pthread.h>

#include "options.h"
#include "caveat.h"
#include "cachesim.h"

option<long> conf_view("view", 0, 10, "Real-time view");

option<long> conf_tcache("tcache", 64*1024, "Binary translation cache size");

extern void* viewer_thread(void*);

extern hart_t* showing;


long clone_proxy(class hart_base_t* h, long* args)
{
  hart_t* child = new hart_t(h);
  return clone_thread(child);
}

void exitfunc()
{
  if (conf_view())
    endwin();
  status_report();
  fprintf(stderr, "\n--------\n");
  for (hart_t* p=hart_t::list(); p; p=p->next()) {
    fprintf(stderr, "Core [%d] ", p->tid());
    p->print();
  }
  fprintf(stderr, "\n");
  status_report();
  fprintf(stderr, "\n");
}



void* status_thread(void*)
{
  for (;;) {
    sleep(1);
    status_report();
  }
}



int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "cachesim: RISC-V cache simulator");
  if (argc == 0)
    help_exit();
  
  showing = new hart_t(argc, argv, envp);
  showing->clone = clone_proxy;
  atexit(exitfunc);
  start_time();

  if (conf_view()) {
    showing->simulator = view_simulator;
    pthread_t viewer;
    pthread_create(&viewer, NULL, viewer_thread, 0);
    showing->interpreter();
  }
  else {
    pthread_t status;
    pthread_create(&status, NULL, status_thread, 0);
    showing->simulator = dumb_simulator;
    showing->interpreter();
  }
}
