
#include <stdlib.h>
#include <stdio.h>

#define poolsize  (1<<30)	/* size of simulation memory pool */

static char simpool[poolsize];	/* base of memory pool */
static volatile char* pooltop = simpool; /* current allocation address */

void *malloc(size_t size)
{
  char volatile *rv, *newtop;
  do {
    volatile char* after = pooltop + size + 16; /* allow for alignment */
    if (after > simpool+poolsize) {
      fprintf(stderr, " failed\n");
      return 0;
    }
    rv = pooltop;
    newtop = (void*)((unsigned long)after & ~0xfL); /* always align to 16 bytes */
  } while (!__sync_bool_compare_and_swap(&pooltop, rv, newtop));
      
  return (void*)rv;
}

void free(void *ptr)
{
  /* we don't free stuff */
}

void *calloc(size_t nmemb, size_t size)
{
  return malloc(nmemb * size);
}

void *realloc(void *ptr, size_t size)
{
  return 0;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
  return 0;
}

