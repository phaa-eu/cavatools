/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
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
#include "caveat.h"
#include "strand.h"

#define THREAD_STACK_SIZE (1<<16)

extern long emulate_brk(long addr);

option<bool> conf_ecall("ecall",	false, true,			"Show system calls");

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

void strand_t::proxy_ecall()
{
  //fprintf(stderr, "proxy_ecall(a0=%ld)\n", xrf[17]);
  long rvnum = xrf[17];
  if (rvnum<0 || rvnum>HIGHEST_ECALL_NUM || !rv_to_host[rvnum].name) {
    fprintf(stderr, "Illegal ecall number %ld\n", rvnum);
    abort();
  }
  long sysnum = rv_to_host[rvnum].sysnum;
  const char* name = rv_to_host[rvnum].name;
  if (sysnum < 0) {
    switch (sysnum) {
    case -1:
      die("No mapping for system call %s to host system", name);
    case -2:
      die("RISCV-V system call %s not supported on host system", name);
    default:
      abort();
    }
  }
  //fprintf(stderr, "ecall %ld --> x86 syscall %ld %s\n", rvnum, sysnum, name);
  if (conf_ecall)
    fprintf(stderr, "Ecall %s\n", name);
  proxy_syscall(sysnum);
}

#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

int thread_interpreter(void* arg)
{
#if 0
  strand_t* oldcpu = (strand_t*)arg;
  strand_t* newcpu = oldcpu->newcore();
  newcpu->write_reg(2, newcpu->read_reg(11)); // a1 = child_stack
  newcpu->write_reg(4, newcpu->read_reg(13)); // a3 = tls
  newcpu->write_reg(10, 0);	// indicating we are child thread
  newcpu->write_pc(newcpu->read_pc()+4); // skip over ecall instruction
  newcpu->set_tid();
  oldcpu->clone_lock = 0;
  futex(&oldcpu->clone_lock, FUTEX_WAKE, 1);
  newcpu->interpreter();
#endif
  abort();
  return 0;
}

void strand_t::proxy_syscall(long sysnum)
{
  long a0=xrf[10], a1=xrf[11], a2=xrf[12], a3=xrf[13], a4=xrf[14], a5=xrf[15];
  long retval=0;
  switch (sysnum) {
    
  case SYS_exit:
  case SYS_exit_group:
    exit(a0);

  case SYS_brk:
    //fprintf(stderr, "SYS_brk(%lx)\n", a0);
    //    retval = emulate_brk(a0, read_pc()>MEM_END ? &dl_linux_info : &prog_info);
    //fprintf(stderr, "current.brk = 0x%lx\n", current.brk);
    retval = emulate_brk(a0);
    break;
    
  case SYS_clone:
    {
      char* interp_stack = new char[THREAD_STACK_SIZE];
      interp_stack += THREAD_STACK_SIZE; // grows down
      long flags = a0 & ~CLONE_SETTLS; // not implementing TLS in interpreter yet
      clone_lock = 1;		       // private mutex
      retval = clone(thread_interpreter, interp_stack, flags, this, (void*)a2, (void*)a4);
      while (clone_lock)
	futex(&clone_lock, FUTEX_WAIT, 1);
    }
    break;
    
  default:
    retval = asm_syscall(sysnum, a0, a1, a2, a3, a4, a5);
  }
  xrf[10] = retval;
}
