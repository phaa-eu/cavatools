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

#include <thread>

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

int maintid;

void strand_t::riscv_syscall()
{
  long a0=s.xrf[10], a1=s.xrf[11], a2=s.xrf[12], a3=s.xrf[13], a4=s.xrf[14], a5=s.xrf[15];
  long rvnum = s.xrf[17];
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
  if (conf_ecall()) {
    int tid = gettid();
    fprintf(stderr, "[%d] Ecall %s( %ld(0x%lx), %ld(0x%lx), %ld(0x%lx), %ld(0x%lx) )", tid, name, a0, a0, a1, a1, a2, a2, a3, a3);
  }
  if (sysnum == SYS_clone)
    s.xrf[10] = hart_pointer->clone(hart_pointer, (long*)s.xrf+10);
  else
    s.xrf[10] = hart_pointer->syscall(hart_pointer, sysnum, (long*)s.xrf+10);
  if (conf_ecall())
    fprintf(stderr, " -> %ld(0x%lx)\n", s.xrf[10], s.xrf[10]);
}

#define prefix(x) (strncmp(path, x, strlen(x))==0)
static char riscv_file[5000];
char* riscv_remap(char* path)
{
  if (prefix("/lib") || prefix("/etc")) {
    const char* sysroot = "/opt/riscv/sysroot";
    strcpy(riscv_file, sysroot);
    strcpy(riscv_file+strlen(sysroot), path);
#if 0
    // hack!
    if (riscv_file[strlen(riscv_file)-1] != '6')
      strcpy(riscv_file+strlen(riscv_file), ".6");
    //
#endif
    dbmsg("mapping %s to %s", path, riscv_file);
    return riscv_file;
  }
  else
    return path;
}

long host_syscall(int sysnum, long* a)
{
  long retval=0;
  switch (sysnum) {
    
  case SYS_exit:
  case SYS_exit_group:
    if (gettid() == maintid) {
      for (hart_base_t* p=hart_base_t::list()->next(); p; p=p->next())
	kill(p->tid(), SIGQUIT);
      throw((int)a[0]);
    }
    else {
      while (1)
	sleep(1000);
    }
    
#if 0
  case SYS_brk:
    //fprintf(stderr, "SYS_brk(%lx)\n", a0);
    //    retval = emulate_brk(a0, read_pc()>MEM_END ? &dl_linux_info : &prog_info);
    //fprintf(stderr, "current.brk = 0x%lx\n", current.brk);
    retval = emulate_brk(a[0]);
    return retval;
#endif
    
  case SYS_clone:
    fprintf(stderr, "Simulator must handle clones!\n");
    abort();

    //  case SYS_mmap:
    //    fprintf(stderr, "mmap flag MAP_FIXED=%d\n", (a[3]&MAP_FIXED)!=0);
    //    break;

#if 1
  case SYS_open:
    a[0] = (long)riscv_remap((char*)a[0]);
    break;
  case SYS_openat:
  case SYS_openat2:
    a[1] = (long)riscv_remap((char*)a[1]);
    break;
#endif
    
  default:
    ;
  }
  retval = asm_syscall(sysnum, a[0], a[1], a[2], a[3], a[4], a[5]);
  return retval;
}
 
long default_syscall_func(class hart_base_t* h, long num, long* args)
{
  return host_syscall(num, args);
}





#define futex(a, b, c)  syscall(SYS_futex, a, b, c, 0, 0, 0)

/*
  RISC-V clone system call arguments not same as wrapper or X86_64:
  a0 = flags
  a1 = child_stack
  a2 = parent_tidptr
  a3 = tls
  a4 = child_tidptr
*/

void thread_interpreter(strand_t* me)
{
  me->tid = gettid();
  futex(&me->tid, FUTEX_WAKE, 1);
  
  me->s.xrf[2] = me->s.xrf[11];	// a1 = child_stack
  me->s.xrf[4] = me->s.xrf[13];	// a3 = tls
  me->s.xrf[10] = 0;		// indicate child thread
  me->pc += 4;			// skip over ecall
  me->interpreter();
}

int clone_thread(hart_base_t* h)
{
  fprintf(stderr, "clone_thread()\n");
  strand_t* child = h->strand;
  child->tid = 0;		// acts as futex lock
  std::thread t(thread_interpreter, child);
  while (child->tid == 0)
    futex(&child->tid, FUTEX_WAIT, 0);
  t.detach();
  return child->tid;
}


void wait_until_zero(volatile int* vp)
{
  while (*vp != 0)
    futex(vp, FUTEX_WAIT, 0);
}

void release_waiter(volatile int* vp)
{
  *vp = 0;
  futex(vp, FUTEX_WAKE, 1);
}
