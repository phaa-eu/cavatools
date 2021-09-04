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

#include "uspike.h"
#include "cpu.h"

#include "elf_loader.h"

#define THREAD_STACK_SIZE (1<<16)

static long pretend_Hz;
static struct timeval start_tv;

void start_time(int mhz)
{
  gettimeofday(&start_tv, 0);
  pretend_Hz = mhz * 1000000;
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

double simulated_time(long cycles)
{
  double seconds = cycles / pretend_Hz;
  seconds += (cycles % pretend_Hz) / 1e6;
  return seconds;
}

long emulate_brk(long addr)
{
  struct pinfo_t* info = &current;
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
  else if (info->brk < newbrk_page) {
    void* rc = mmap((void*)info->brk, newbrk_page - info->brk, -1, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    if(rc != (void*)info->brk) {
      fprintf(stderr, "Unable to mmap() in emulate_brk()\n");
      abort();
    }
  }
  info->brk = newbrk_page;

  return newbrk;
}

static int thread_interpreter(void* arg)
{
  cpu_t* newcpu = (class cpu_t*)arg;
  newcpu->write_reg(2, newcpu->read_reg(11)); // a1 = child_stack
  newcpu->write_reg(4, newcpu->read_reg(13)); // a3 = tls
  newcpu->write_reg(10, 0);	// indicating we are child thread
  newcpu->write_pc(newcpu->read_pc()+4); // skip over ecall instruction
  
  enum stop_reason reason;
  //conf.show = true;
  //  sleep(100);
  //fprintf(stderr, "starting thread interpreter, tid=%d, tp=%lx\n", gettid(), READ_REG(4));
  do {
    reason = interpreter(newcpu, 10000);
    //status_report();
  } while (reason == stop_normal);
  status_report();
  fprintf(stderr, "\n");
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  else if (reason != stop_exited)
    die("unknown reason %d", reason);
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

bool cpu_t::proxy_ecall(long cycles)
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
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "\n%12ld %8lx: ecalls=%ld %s:%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)",
	    cycles, read_pc(), ecall_count, name, sysnum, a0, a1, a2, a3, a4, a5);
  long retval = 0;
  switch (sysnum) {
  case -1:
    fprintf(stderr, "No mapping for system call %s to host system\n", name);
    abort();
  case -2:
    fprintf(stderr, "RISCV-V system call %s not supported on host system\n", name);
    abort();

#if 0
  case SYS_brk:
    return emulate_brk(a0);
#endif

  case SYS_exit:
  case SYS_exit_group:
    return true;

  case SYS_rt_sigaction:
    fprintf(stderr, "rt_sigaction called\n");
    retval = 0;
    break;

#if 0
  case SYS_rt_sigprocmask:
    fprintf(stderr, "rt_sigprocmask called\n");
    retval = 0;
    break;
#endif

  case SYS_clock_gettime:
  case SYS_gettimeofday:
    {
      struct timeval tv;
      tv.tv_sec  = cycles / pretend_Hz;
      tv.tv_usec = cycles % pretend_Hz;
      tv.tv_sec  = start_tv.tv_sec  + (cycles/pretend_Hz);
      tv.tv_usec = start_tv.tv_usec + (cycles%pretend_Hz);
      while (tv.tv_usec > 1000000) {
	tv.tv_sec  += 1;
	tv.tv_usec -= 1000000;
      }
      memcpy((void*)(sysnum==SYS_gettimeofday? a0 : a1), &tv, sizeof tv);
      retval = 0;
    }
    break;

  case SYS_times:
    {
      struct tms tms_buf;
      memset(&tms_buf, 0, sizeof tms_buf);
      tms_buf.tms_utime = (double)cycles * sysconf(_SC_CLK_TCK) / pretend_Hz;
      memcpy((void*)a0, &tms_buf, sizeof tms_buf); 
      retval = 0;
    }
    break;

  case SYS_close:
    if (a0 <= 2) // Don't close stdin, stdout, stderr
      return 0;
    goto default_case;
    return true;

  case SYS_clone:
    {
      char* interp_stack = new char[THREAD_STACK_SIZE];
      interp_stack += THREAD_STACK_SIZE; // grows down
      long flags = a0 & ~CLONE_SETTLS; // not implementing TLS in interpreter yet
      cpu_t* newcpu = new cpu_t(this);
      retval = clone(thread_interpreter, interp_stack, flags, newcpu, (void*)a2, (void*)a4);
    }
    break;
    
  default:
  default_case:
    retval = asm_syscall(sysnum, a0, a1, a2, a3, a4, a5);
    break;
  }
  write_reg(10, retval);
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "->0x%lx", read_reg(10));
  return false;
}
