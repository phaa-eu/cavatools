/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#define abort() { fprintf(stderr, "Aborting in %s line %d\n", __FILE__, __LINE__); exit(-1); }

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <signal.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <sys/times.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "cache.h"
#include "core.h"

core_t* core;			/* array of pointers to cores */
perf_t perf;

/* maxcores=0 means get from shared memory */
void perf_init(const char* shm_name, int maxcores)
{
  int n;			/* number of instruction parcels */
  long sz;			/* size of shared segment */
  if (maxcores > 0) {
    n = (high_bound - low_bound) / 2;
    sz = sizeof(struct perf_header_t);
    sz += maxcores * sizeof(core_t);
    sz += n * sizeof(struct insn_t);
    sz += maxcores * n * sizeof(struct count_t);
    sz += maxcores * n * sizeof(long) * 2;
    int fd = shm_open(shm_name, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
    dieif(fd<0, "shm_open() failed in perf_create");
    dieif(ftruncate(fd, sz)<0, "ftruncate() failed in perf_create");
    perf.h = (struct perf_header_t*)mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    dieif(perf.h==0, "mmap() failed in perf_create");
    memset((void*)perf.h, 0, sz);
    perf.h->size = sz;
    perf.h->base = low_bound;
    perf.h->bound = high_bound;
    perf.h->cores = maxcores;
  }
  else {
    int fd = shm_open(shm_name, O_RDONLY, 0);
    dieif(fd<0, "shm_open() failed in perf_open");
    perf.h = (struct perf_header_t*)mmap(0, sizeof(struct perf_header_t), PROT_READ, MAP_SHARED, fd, 0);    
    dieif(perf.h==0, "first mmap() failed in perf_open");
    sz = perf.h->size;
    dieif(munmap((void*)perf.h, sizeof(struct perf_header_t))<0, "munmap() failed in perf_open");
    perf.h = (struct perf_header_t*)mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
    dieif(perf.h==0, "second mmap() failed in perf_open");
    n = (perf.h->bound - perf.h->base) / 2;
    fprintf(stderr, "base=%lx, bound=%lx, cores=%d, active=%d\n",
	    perf.h->base, perf.h->bound, perf.h->cores, perf.h->active);
  }
  perf.core = (core_t*)(perf.h+1);
  perf.insn_array = (struct insn_t*)(perf.core + perf.h->cores); 
  if (maxcores) {
    memset(perf.insn_array, 0, n*sizeof(struct insn_t));
    for (Addr_t pc=perf.h->base; pc<perf.h->bound; pc+=2)
      decode_instruction(&perf.insn_array[(pc-perf.h->base)/2], pc);
  }
  char* s = (char*)(perf.insn_array + n);
  perf.count = (struct count_t**)malloc(perf.h->cores * sizeof(void*));
  for (int i=0; i<perf.h->cores; i++) {
    perf.count[i] = (struct count_t*)s;
    s += n*sizeof(struct count_t);
  }
  perf.icmiss = (long**)malloc(perf.h->cores * sizeof(long*));
  for (int i=0; i<perf.h->cores; i++) {
    perf.icmiss[i] = (long*)s;
    s += n*sizeof(long);
  }
  perf.dcmiss = (long**)malloc(perf.h->cores * sizeof(long*));
  for (int i=0; i<perf.h->cores; i++) {
    perf.dcmiss[i] = (long*)s;
    s += n*sizeof(long);
  }
  assert(s == (char*)perf.h+sz);
  assert(perf.h->size == sz);
}

void perf_close()
{
  dieif(munmap((void*)perf.h, perf.h->size)<0, "munmap() failed in perf_close");
}
