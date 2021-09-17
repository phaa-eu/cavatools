#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "options.h"
#include "uspike.h"
#include "perf.h"

void perf_t::common_init()
{
  _count = new count_t*[h->max_cores];
  _imiss = new long*[h->max_cores];
  _dmiss = new long*[h->max_cores];
  char* a = (char*)h->arrays;
  for (int k=0; k<h->max_cores; k++) {
    _count[k] = (count_t*)a; a += h->parcels*sizeof(count_t);
    _imiss[k] = (long*)a;    a += h->parcels*sizeof(long);
    _dmiss[k] = (long*)a;    a += h->parcels*sizeof(long);
  }
}

perf_t::perf_t(long base, long bound, long n, const char* shm_name)
{
  long sz = sizeof(perf_header_t);
  long p = (bound-base)/2;
  sz += p*n*sizeof(count_t);	// execution counters
  sz += 2*p*n*sizeof(long);	// cache miss counters
  name = shm_name;
  fd = shm_open(name, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
  dieif(fd<0, "shm_open() failed");
  dieif(ftruncate(fd, sz)<0, "ftruncate() failed");
  h = (perf_header_t*)mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  dieif(h==0, "mmap() failed");
  memset(h, 0, sz);
  h->size = sz;
  h->base = base;
  h->parcels = p;
  h->cores = 0;
  h->max_cores = n;
  common_init();
}

perf_t::perf_t(const char* shm_name)
{
  name = shm_name;
  fd = shm_open(name, O_RDONLY, 0);
  dieif(fd<0, "shm_open() failed in perf_open");
  h = (perf_header_t*)mmap(0, sizeof(perf_header_t), PROT_READ, MAP_SHARED, fd, 0);
  dieif(h==0, "first mmap() failed");
  long sz = h->size;
  dieif(munmap((void*)h, sizeof(perf_header_t))<0, "munmap() failed");
  h = (perf_header_t*)mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
  dieif(h==0, "second mmap() failed");
  common_init();
}

perf_t::~perf_t()
{
  //  dieif(munmap((void*)h, h->size)<0, "munmap() failed");
  shm_unlink(name);
  close(fd);
  delete _count;
  delete _imiss;
  delete _dmiss;
}

