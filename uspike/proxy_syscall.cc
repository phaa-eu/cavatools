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
#include <sys/stat.h>

#include "options.h"
#include "uspike.h"
#include "mmu.h"
#include "hart.h"

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

extern option<bool> conf_ecall;
extern option<long> conf_report;

void hart_t::proxy_ecall()
{
  long rvnum = read_reg(17);
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
  if (conf_ecall)
    fprintf(stderr, "Ecall[%ld] %s\n", tid(), name);
  proxy_syscall(sysnum);
}

#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

static long dummy;

int thread_interpreter(void* arg)
{
  hart_t* oldcpu = (hart_t*)arg;
  hart_t* newcpu = oldcpu->newcore();
  newcpu->write_reg(2, newcpu->read_reg(11)); // a1 = child_stack
  newcpu->write_reg(4, newcpu->read_reg(13)); // a3 = tls
  newcpu->write_reg(10, 0);	// indicating we are child thread
  newcpu->write_pc(newcpu->read_pc()+4); // skip over ecall instruction
  newcpu->set_tid();
  oldcpu->clone_lock = 0;
  futex(&oldcpu->clone_lock, FUTEX_WAKE, 1);
  newcpu->run_thread();
  return 0;
}

/*
 * Workaround for differences between RISC-V and X86_64 struct stat
 */
struct rv_stat {
  uint64_t st_dev;		// at  0
  uint64_t st_ino;		// at  8
  uint32_t st_mode;		// at 16
  uint32_t st_nlink;		// at 20
  uint32_t st_uid;		// at 24
  uint32_t st_gid;		// at 28
  uint64_t st_remaining[12];	// at 30
};
static_assert(sizeof(rv_stat) == 128);

static inline void copy_stat(rv_stat* rv, struct stat* x)
{
  memcpy(rv, x, sizeof(rv_stat));
  rv->st_mode	= x->st_mode;
  rv->st_nlink	= x->st_nlink;
  rv->st_uid	= x->st_uid;
  rv->st_gid	= x->st_gid;
}

int fork_interpreter(void* arg)
{
  hart_t::cpu_list = 0;			// 
  hart_t* oldcpu = (hart_t*)arg;
  hart_t* newcpu = oldcpu->newcore();
  //  newcpu->write_reg(2, newcpu->read_reg(11)); // a1 = child_stack
  //  newcpu->write_reg(4, newcpu->read_reg(13)); // a3 = tls
  newcpu->write_reg(10, 0);	// indicating we are child thread
  newcpu->write_pc(newcpu->read_pc()+4); // skip over ecall instruction
  newcpu->set_tid();
  newcpu->run_thread();
  return 0;
}

void hart_t::proxy_syscall(long sysnum)
{
  long a0=read_reg(10), a1=read_reg(11), a2=read_reg(12), a3=read_reg(13), a4=read_reg(14), a5=read_reg(15);
  long retval=0;
  switch (sysnum) {
  case SYS_exit:
  case SYS_exit_group:
    exit(a0);
    //case SYS_sched_yield:
    //break;
  case SYS_clone:
    {
      /*
	RISC-V clone system call arguments not same as wrapper or X86_64:
	a0 = flags
	a1 = child_stack
	a2 = parent_tidptr
	a3 = tls
	a4 = child_tidptr
      */
      char* interp_stack = new char[THREAD_STACK_SIZE];
      interp_stack += THREAD_STACK_SIZE; // grows down
      //fprintf(stderr, "clone: settls=%d, tp=%lx, a3(tls)=%lx\n", (a0 & CLONE_SETTLS)!=0, read_reg(4), a3);
      long flags = a0 & ~CLONE_SETTLS; // not implementing TLS in interpreter yet
      if (flags & CLONE_VM) {
	clone_lock = 1;		       // private mutex
	retval = clone(thread_interpreter, interp_stack, flags, this, (void*)a2, (void*)a4);
	while (clone_lock)
	  futex(&clone_lock, FUTEX_WAIT, 1);
      }
      else
	retval = clone(fork_interpreter, interp_stack, flags, this, (void*)a2, (void*)a4);
    }
    break;
    /* Workaround for size mismatch between struct stat in riscv and x86_64 Linux */
  case SYS_stat:
  case SYS_fstat:
  case SYS_lstat:
    {
      struct stat x;
      retval = asm_syscall(sysnum, a0, (long)&x, a2, a3, a4, a5);
      copy_stat((rv_stat*)a1, &x);
      break;
    }
  case SYS_newfstatat:
    {
      struct stat x;
      retval = asm_syscall(sysnum, a0, a1, (long)&x, a3, a4, a5);
      copy_stat((rv_stat*)a2, &x);
      break;
    }
  case SYS_ppoll:
    {
      a2 = -1;
      sysnum = SYS_poll;
      retval = asm_syscall(sysnum, a0, a1, a2, a3, a4, a5);
      break;
    }
    /*
  case SYS_rt_sigaction:
    fprintf(stderr, "SYS_rt_sigaction()\n");
    break;
    
  case SYS_rt_sigprocmask:
  case SYS_pause:
  case SYS_kill:
  case SYS_rt_sigpending:
  case SYS_rt_sigtimedwait:
  case SYS_rt_sigqueueinfo:
  case SYS_rt_sigsuspend:
  case SYS_sigaltstack:
    fprintf(stderr, "System call %ld\n", sysnum);
    */
    
  default:
    retval = asm_syscall(sysnum, a0, a1, a2, a3, a4, a5);
  }
  write_reg(10, retval);
}
