#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

const long N = 10000000;

void sequential(cache_t* c)
{
  for (long i=0; i<N; i++)
    c->lookup(i*8, false, i);
  printf("%s hit = %6.2f%%\n", c->name(), 100.0*c->hits()/c->refs());
  delete c;
}

void random(cache_t* c)
{
  for (long i=0; i<N; i++)
    c->lookup((rand() % 20480)*8, false, i);
  printf("%s hit = %6.2f%%\n", c->name(), 100.0*c->hits()/c->refs());
  delete c;
}

int main()
{
  long m;
  cache_t* c;

  sequential(new cache_t("Sequential 1 way no prefetch", 1, 6, 8, false));
  sequential(new cache_t("Sequential 4 way no prefetch", 4, 6, 6, false));
  sequential(new cache_t("Sequential 1 way with prefetch", 1, 6, 8, false, true));
  sequential(new cache_t("Sequential 4 way with prefetch", 4, 6, 6, false, true));

  random(new cache_t("Random 1 way no prefetch", 1, 6, 8, false));
  random(new cache_t("Random 2 way no prefetch", 2, 6, 7, false));
  random(new cache_t("Random 4 way no prefetch", 4, 6, 6, false));

  random(new cache_t("Random 1 way with prefetch", 1, 6, 8, false, true));
  random(new cache_t("Random 2 way with prefetch", 2, 6, 7, false, true));
  random(new cache_t("Random 4 way with prefetch", 4, 6, 6, false, true));
}
