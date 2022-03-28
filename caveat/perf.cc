/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "options.h"
#include "uspike.h"
#include "perf.h"

perf_header_t* perf_t::h;

perf_t::perf_t(long n)
{
  dieif(n>=h->_cores, "exceeding %ld allocated cores, use --cores=n\n", h->_cores);
  char* ptr = h->arrays + n*h->parcels*(sizeof(count_t) + sizeof(long));
  _count = (count_t*)ptr;
  _dmiss = (long*)(ptr + h->parcels*sizeof(count_t));
  _number = n;
}

void perf_t::create(long base, long bound, long n, const char* shm_name)
{
  long sz = sizeof(perf_header_t);
  long p = (bound-base)/2;
  sz += p*n*sizeof(count_t);	// execution counters
  sz += p*n*sizeof(long);	// cache miss counters
  int fd = shm_open(shm_name, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
  dieif(fd<0, "shm_open() failed");
  dieif(ftruncate(fd, sz)<0, "ftruncate() failed");
  h = (perf_header_t*)mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  dieif(h==0, "mmap() failed");
  memset(h, 0, sz);
  h->size = sz;
  h->base = base;
  h->parcels = p;
  h->_cores = n;
}

void perf_t::open(const char* shm_name)
{
  int fd = shm_open(shm_name, O_RDONLY, 0);
  dieif(fd<0, "shm_open(%s) failed in perf_open", shm_name);
  struct stat stat;
  dieif(fstat(fd, &stat)!=0, "could not fstat %s\n", shm_name);
  h = (perf_header_t*)mmap(0, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  dieif(h==0, "mmap() of %s failed", shm_name);
}

void perf_t::close(const char* shm_name)
{
  shm_unlink(shm_name);
}
  
