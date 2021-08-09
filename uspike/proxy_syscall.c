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
//#include <pthread.h>
//#include <signal.h>

#include "processinfo.h"

static long pretend_Hz;
static struct timeval start_tv;

void start_time(int mhz)
{
  gettimeofday(&start_tv, 0);
  pretend_Hz = mhz * 1000000;
}

static long emulate_brk(long addr, struct pinfo_t* info)
{
  long newbrk = addr;
  if (addr < info->brk_min)
    newbrk = info->brk_min;
  else if (addr > info->brk_max)
    newbrk = info->brk_max;

  if (info->brk == 0)
    info->brk = ROUNDUP(info->brk_min, RISCV_PGSIZE);

  uintptr_t newbrk_page = ROUNDUP(newbrk, RISCV_PGSIZE);
  if (info->brk > newbrk_page)
    munmap((void*)newbrk_page, info->brk - newbrk_page);
  else if (info->brk < newbrk_page)
    assert(mmap((void*)info->brk, newbrk_page - info->brk, -1, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0, 0) == (void*)info->brk);
  info->brk = newbrk_page;

  return newbrk;
}

int proxy_syscall(long sysnum, long cycles, const char* name, long a0, long a1, long a2, long a3, long a4, long a5)
{
  fprintf(stderr, "ecall %s:%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
	  name, sysnum, a0, a1, a2, a3, a4, a5);
  switch (sysnum) {
  case -1:
    fprintf(stderr, "No mapping for system call %s to host system\n", name);
    abort();
  case -2:
    fprintf(stderr, "RISCV-V system call %s not supported on host system\n", name);
    abort();
    
#if 1
  case __NR_brk:
    return emulate_brk(a0, &current);
#endif

  case __NR_exit:
  case __NR_exit_group:
    fprintf(stderr, "should never proxy exit and exit_group");
    abort();

  case __NR_rt_sigaction:
    fprintf(stderr, "rt_sigaction called\n");
    return 0;

  case __NR_rt_sigprocmask:
    fprintf(stderr, "rt_sigprocmask called\n");
    return 0;

  case __NR_clone: /* sys_clone */
    fprintf(stderr, "sys_clone not implemented\n");
    abort();

  case __NR_clock_gettime:
  case __NR_gettimeofday:
    {
      struct timeval tv;
      tv.tv_sec  = cycles / pretend_Hz;
      tv.tv_usec = cycles % pretend_Hz;
      tv.tv_sec  += start_tv.tv_sec;
      tv.tv_usec += start_tv.tv_usec;
      tv.tv_sec  += tv.tv_usec / 1000000;  // microseconds overflow
      tv.tv_usec %=              1000000;
      memcpy((void*)(sysnum==__NR_gettimeofday? a0 : a1), &tv, sizeof tv);
    }
    return 0;

  case __NR_times:
    {
      struct tms tms_buf;
      memset(&tms_buf, 0, sizeof tms_buf);
      tms_buf.tms_utime = (double)cycles * sysconf(_SC_CLK_TCK) / pretend_Hz;
      memcpy((void*)a0, &tms_buf, sizeof tms_buf);
    }
    return 0;

  case __NR_close:
    if (a0 <= 2) // Don't close stdin, stdout, stderr
      return 0;
    goto default_case;

  default:
  default_case:
    return syscall(sysnum, a0, a1, a2, a3, a4, a5);
  }
  abort(); // should never get here
}
