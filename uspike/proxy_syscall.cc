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
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/times.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <sched.h>

#include "options.h"
#include "uspike.h"
#include "mmu.h"
#include "cpu.h"

#include "elf_loader.h"

#define THREAD_STACK_SIZE (1<<16)

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

static int thread_interpreter(void* arg)
{
  cpu_t* newcpu = (class cpu_t*)arg;
  newcpu->write_reg(2, newcpu->read_reg(11)); // a1 = child_stack
  newcpu->write_reg(4, newcpu->read_reg(13)); // a3 = tls
  newcpu->write_reg(10, 0);	// indicating we are child thread
  newcpu->write_pc(newcpu->read_pc()+4); // skip over ecall instruction
  interpreter(newcpu);
  return 0;
}

static __inline long asm_syscall(long sysnum, long a0, long a1, long a2, long a3, long a4, long a5)
{
  long retval;
  register long r10 __asm__("r10") = a3;
  register long r8  __asm__("r8")  = a4;
  register long r9  __asm__("r9")  = a5;
  __asm__ __volatile__
    ("syscall"
     : "=a"(retval) /* output */
     : "a"(sysnum), /* input */
       "D"(a0),
       "S"(a1),
       "d"(a2),
       "r"(r10),
       "r"(r8),
       "r"(r9)
     : "rcx", /* clobber */
       "r11",
       "memory"
     );
  return retval;
}

struct syscall_map_t {
  int sysnum;
  const char*name;
};

static struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"
};

bool cpu_t::proxy_ecall()
{
  static long ecall_count;
  long rvnum = read_reg(17);
  if (rvnum<0 || rvnum>HIGHEST_ECALL_NUM || !rv_to_host[rvnum].name) {
    fprintf(stderr, "Illegal ecall number %ld\n", rvnum);
    abort();
  }
  long sysnum = rv_to_host[rvnum].sysnum;
  long a0=read_reg(10), a1=read_reg(11), a2=read_reg(12), a3=read_reg(13), a4=read_reg(14), a5=read_reg(15);
  const char* name = rv_to_host[rvnum].name;
  ecall_count++;
  if (conf_ecall)
    fprintf(stderr, "\n%8lx: ecalls=%ld %s:%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)",
	    read_pc(), ecall_count, name, sysnum, a0, a1, a2, a3, a4, a5);
  long retval = 0;
  switch (sysnum) {
  case -1:
    fprintf(stderr, "No mapping for system call %s to host system\n", name);
    abort();
  case -2:
    fprintf(stderr, "RISCV-V system call %s not supported on host system\n", name);
    abort();

#if 0
  case 262:			// newfstatat
    sysnum = 4;			// -> stat
    goto default_case;
#endif

  case SYS_exit:
  case SYS_exit_group:
    exit(a0);

  case SYS_clone:
    {
      char* interp_stack = new char[THREAD_STACK_SIZE];
      interp_stack += THREAD_STACK_SIZE; // grows down
      long flags = a0 & ~CLONE_SETTLS; // not implementing TLS in interpreter yet
      cpu_t* newcpu = new cpu_t(this, new mmu_t());
      retval = clone(thread_interpreter, interp_stack, flags, newcpu, (void*)a2, (void*)a4);
    }
    break;
    
  default:
  default_case:
    retval = asm_syscall(sysnum, a0, a1, a2, a3, a4, a5);
    break;
  }
  write_reg(10, retval);
  if (conf_ecall)
    fprintf(stderr, "->0x%lx", read_reg(10));
  return false;
}
