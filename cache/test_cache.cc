#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cache.h"

const long N = 100000000;
long numbers[N];

static timeval start_tv;

void start_time()
{
  gettimeofday(&start_tv, 0);
}

double elapse_time()
{
  struct timeval now_tv;
  gettimeofday(&now_tv, 0);
  struct timeval *t0=&start_tv, *t1=&now_tv;
  double seconds = t1->tv_sec + t1->tv_usec/1e6;
  seconds       -= t0->tv_sec + t0->tv_usec/1e6;
  return seconds;
}

void sequential(cache_t* c)
{
  start_time();
  for (long i=0; i<N; i++)
    c->read(i<<3);
  double t = elapse_time();
  printf("%s %ld ways %gns miss=%6.2f%%\n", c->name(), c->ways(), 1e9*t/N, 100.0*c->misses()/c->refs());
  delete c;
}

void random(cache_t* c)
{
  start_time();
  for (long i=0; i<N; i++)
    c->read(numbers[i]);
  double t = elapse_time();
  printf("%s %gns miss=%6.2f%%\n", c->name(), 1e9*t/N, 100.0*c->misses()/c->refs());
  delete c;
}

int main()
{
  for (long i=0; i<N; i++)
    numbers[i] = (rand() % (1<<12)) << 3;
  
  sequential(new fsm_cache<1, false, false>(6, 8, "Sequential no prefetch"));
  sequential(new fsm_cache<1, false, true >(6, 8, "Sequential with prefetch"));
  sequential(new fsm_cache<2, false, false>(6, 7, "Sequential no prefetch"));
  sequential(new fsm_cache<2, false, true >(6, 7, "Sequential with prefetch"));
  sequential(new fsm_cache<4, false, false>(6, 6, "Sequential no prefetch"));
  sequential(new fsm_cache<4, false, true >(6, 6, "Sequential with prefetch"));
  /*  
  random(new fsm_cache<1, false, false>("Random no prefetch",   6, 8));
  random(new fsm_cache<1, false, true >("Random with prefetch", 6, 8));
  random(new fsm_cache<4, false, false>("Random no prefetch",   6, 6));
  random(new fsm_cache<4, false, true >("Random with prefetch", 6, 6));
  */
}
