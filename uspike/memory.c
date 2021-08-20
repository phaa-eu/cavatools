
#include <stdlib.h>
#include <stdio.h>

#define poolsize  (1<<28)	/* size of simulation memory pool */

static char simpool[poolsize];	/* base of memory pool */
static char* pooltop = simpool;	/* current allocation address */

void *malloc(size_t size)
{
  fprintf(stderr, "malloc(%ld)", size);
  char* after = pooltop + size + 16; /* allow for alignment */
  if (after > simpool+poolsize) {
    fprintf(stderr, " failed\n");
    return 0;
  }
  void* rv = pooltop;
  pooltop = (void*)((unsigned long)after & ~0xfL); /* always align to 16 bytes */
  fprintf(stderr, " return %p\n", rv);
  return rv;
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

