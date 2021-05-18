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

struct core_t* core;		/* array of pointers to cores */
int active_cores = 1;		/* main thread */
struct perf_t perf;

unsigned long lrsc_set = 0;	/* global atomic lock */


void init_core(struct core_t* cpu, struct core_t* parent, Addr_t entry_pc, Addr_t stack_top, Addr_t thread_ptr)
{
  if (parent)
    memcpy(cpu, parent, sizeof(struct core_t));
  else {
    memset(cpu, 0, sizeof(struct core_t));
    for (int i=32; i<64; i++)	/* initialize FP registers to boxed float 0 */
      cpu->reg[i].ul = 0xffffffff00000000UL;
  }
  cpu->pc = entry_pc;
  cpu->reg[SP].l = stack_top;
  cpu->reg[TP].l = thread_ptr;
  /* start with empty caches */
  init_cache(&cpu->icache, "Instruction", conf.ipenalty, conf.iways, conf.iline, conf.irows, 0);
  init_cache(&cpu->dcache, "Data",        conf.dpenalty, conf.dways, conf.dline, conf.drows, 1);
  cpu->state.coreid = cpu - core;
  cpu->perf.start_tick = clock();
  gettimeofday(&cpu->perf.start_timeval, 0);
}

int run_program(struct core_t* cpu)
{
  if (conf.breakpoint)
    insert_breakpoint(conf.breakpoint);
  int fast_mode = 1;
  while (1) {	       /* terminated by program making exit() ecall */
    if (fast_mode)
      fast_sim(cpu);
    else
      slow_sim(cpu);
    if (cpu->state.mcause != 3) /* Not breakpoint */
      break;
    if (fast_mode) {
      if (--cpu->perf.after > 0 || /* not ready to trace yet */
	  --cpu->perf.skip > 0) {  /* only trace every n call */
	cpu->perf.skip = conf.every;
	/* put instruction back */
	decode_instruction(insn(cpu->pc), cpu->pc);
	cpu->state.mcause = 0;
	single_step(cpu);
	/* reinserting breakpoint at subroutine entry */
	insert_breakpoint(conf.breakpoint);
      }
      else { /* insert breakpoint at subroutine return */
	if (cpu->reg[RA].a)	/* _start called with RA==0 */
	  insert_breakpoint(cpu->reg[RA].a);
	fast_mode = 0;		/* start tracing */
      }
    }
    else {  /* reinserting breakpoint at subroutine entry */
      insert_breakpoint(conf.breakpoint);
      fast_mode = 1;		/* stop tracing */
    }
    cpu->state.mcause = 0;
    decode_instruction(insn(cpu->pc), cpu->pc);
  }
  if (cpu->state.mcause == 8) { /* only exit() ecall not handled */
    //cpu->perf.insn_executed++;	/* don't forget to count last ecall */
    return cpu->reg[10].i;
  }
  /* The following cases do not fall out */
  switch (cpu->state.mcause) {
  case 2:
    fprintf(stderr, "Illegal instruction at 0x%08lx\n", cpu->pc);
    GEN_SEGV;
  case 10:
    fprintf(stderr, "Unknown instruction at 0x%08lx\n", cpu->pc);
    GEN_SEGV;
  default:
    abort();
  }
}

void perf_init(const char* shm_name, int reader)
{
  int n;			/* number of instruction parcels */
  long sz;			/* size of shared segment */
  char* s;			/* working pointer */
  if (!reader) {
    n = (insnSpace.bound - insnSpace.base) / 2;
    sz = sizeof(struct perf_header_t);
    sz += n * sizeof(struct insn_t);
    sz += conf.cores * sizeof(struct core_t);
    sz += conf.cores * n * sizeof(struct count_t);
    sz += conf.cores * n * sizeof(long) * 2;
    int fd = shm_open(shm_name, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
    dieif(fd<0, "shm_open() failed in perf_create");
    dieif(ftruncate(fd, sz)<0, "ftruncate() failed in perf_create");
    perf.h = (struct perf_header_t*)mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    dieif(perf.h==0, "mmap() failed in perf_create");
    s = (char*)perf.h;
    memset(s, 0, sz);
    perf.h->size = sz;
    perf.h->cores = conf.cores;
    perf.h->base  = insnSpace.base;
    perf.h->bound = insnSpace.bound;
  }
  else {
    int fd = shm_open(shm_name, O_RDONLY, 0);
    dieif(fd<0, "shm_open() failed in perf_open");
    perf.h = (struct perf_header_t*)mmap(0, sizeof(struct perf_header_t), PROT_READ, MAP_SHARED, fd, 0);    
    dieif(perf.h==0, "first mmap() failed in perf_open");
    sz = perf.h->size;
    dieif(munmap(perf.h, sizeof(struct perf_header_t))<0, "munmap() failed in perf_open");
    perf.h = (struct perf_header_t*)mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
    dieif(perf.h==0, "second mmap() failed in perf_open");
    n = (perf.h->bound - perf.h->base) / 2;
    s = (char*)perf.h;
    insnSpace.base = perf.h->base;
    insnSpace.bound = perf.h->bound;
  }
  s += sizeof(struct perf_header_t);
  perf.insn_array = (struct insn_t*)s;
  s += n*sizeof(struct insn_t);
  perf.core = (struct core_t*)s;
  s += perf.h->cores * sizeof(struct core_t);
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
}

void perf_close()
{
  dieif(munmap(perf.h, perf.h->size)<0, "munmap() failed in perf_close");
}


void final_status()
{
  clock_t end_tick = clock();
  fprintf(stderr, "\n\n");
  for (int i=0; i<active_cores; i++) {
    struct core_t* cpu = &core[i];
    if (cpu->tid == 0)
      continue;
    struct timeval *t1=&cpu->perf.start_timeval, t2;
    gettimeofday(&t2, 0);
    double msec = (t2.tv_sec - t1->tv_sec)*1000;
    msec += (t2.tv_usec - t1->tv_usec)/1000.0;
    double mips = cpu->perf.insn_executed / (1e3*msec);
    fprintf(stderr, "%sCore[%d] executed %ld instructions (%ld system calls) in %3.1f seconds for %3.1f MIPS%s\n",
	    color(cpu->tid), i, cpu->perf.insn_executed, cpu->perf.ecalls, msec/1e3, mips, nocolor);
  }
}
