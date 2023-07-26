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

#include "caveat.h"
#include "hart.h"

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

void hart_t::riscv_syscall()
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
  long rv;
  if (sysnum == SYS_clone)
    //    s.xrf[10] = hart_pointer->clone(hart_pointer, (long*)s.xrf+10);
    rv = clone(this);
  else
    rv = syscall(this, sysnum, a0, a1, a2, a3, a4, a5);
  if (conf_ecall())
    fprintf(stderr, " -> %ld(0x%lx)\n", rv, rv);
  
#ifdef SPIKE
  //  (*p->get_state()).XPR.write(10, rv);
  WRITE_REG(10, rv);
#else
  s.xrf[10] = rv;
#endif
}

#define prefix(x) (strncmp(path, x, strlen(x))==0)
static char riscv_file[5000];
char* riscv_remap(char* path)
{
  if (prefix("/lib") || prefix("/etc")) {
    const char* sysroot = "/opt/riscv/sysroot";
    strcpy(riscv_file, sysroot);
    strcpy(riscv_file+strlen(sysroot), path);
    dbmsg("mapping '%s' to '%s'", path, riscv_file);
    return riscv_file;
  }
  else
    return path;
}

uintptr_t host_syscall(int sysnum, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
  long retval=0;
  switch (sysnum) {

  case SYS_exit:
  case SYS_exit_group:
    goto stop;
    
#if 0
  case SYS_brk:
    //fprintf(stderr, "SYS_brk(%lx)\n", a0);
    //    retval = emulate_brk(a0, read_pc()>MEM_END ? &dl_linux_info : &prog_info);
    //fprintf(stderr, "current.brk = 0x%lx\n", current.brk);
    retval = emulate_brk(a0);
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
    a0 = (long)riscv_remap((char*)a0);
    break;
  case SYS_openat:
  case SYS_openat2:
    a1 = (long)riscv_remap((char*)a1);
    break;
#endif
    
  default:
    ;
  }
  retval = asm_syscall(sysnum, a0, a1, a2, a3, a4, a5);
  return retval;

 stop:
  // main process tid is last on list
  int main_tid = 0;
  for (hart_t* p=hart_t::list(); p; p=p->next())
    main_tid = p->tid();
  if (gettid() != main_tid) {
    while (1)
      sleep(1000);
  }
  exit(a0);
}
 
uintptr_t default_syscall_func(class hart_t* h, int num, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
  return host_syscall(num,  a0, a1, a2, a3, a4, a5);
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

void thread_interpreter(hart_t* me)
{
  me->_tid = gettid();
  futex(&me->_tid, FUTEX_WAKE, 1);

#ifdef SPIKE
  processor_t* p = &me->s.spike_cpu;
  WRITE_REG(2, READ_REG(11));	// a1 = child_stack
  WRITE_REG(4, READ_REG(13));	// a3 = tls
  WRITE_REG(10, 0);		// indicate child thread
#else
  me->s.xrf[2] = me->s.xrf[11]; // a1 = child_stack
  me->s.xrf[4] = me->s.xrf[13]; // a3 = tls
  me->s.xrf[10] = 0;		// indicate child thread
#endif
  me->pc += 4;			// skip over ecall
  me->interpreter();
}

int clone_thread(hart_t* child)
{
  child->_tid = 0;		// acts as futex lock
  std::thread t(thread_interpreter, child);
  while (child->_tid == 0)
    futex(&child->_tid, FUTEX_WAIT, 0);
  t.detach();
  return child->_tid;
}
