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
#include <sys/utsname.h>
#include <sys/times.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#include "caveat_fp.h"
#include "arith.h"

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "cache.h"
#include "core.h"
#include "riscv-opc.h"
#include "ecall_nums.h"


#include <signal.h>

Addr_t guest_handler[_NSIG];

/*
 * Generic signal handler proxy.
 */
void proxy_sa_handler(int signum)
{
  fprintf(stderr, "proxy_sa_handler(%d) called\n", signum);
  /* First figure out which core signal came from */
  struct core_t* cpu = 0;
  pid_t my_tid = syscall(SYS_gettid);
  for (int i=0; i<conf.cores; i++)
    if (core[i].tid == my_tid) {
      cpu = &core[i];
      break;
    }
  if (!cpu) {
    fprintf(stderr, "Cannot find core with tid=%d\n", my_tid);
    exit(-1);
  }
  /* Push PC, fcsr onto stack, then registers */
  assert(sizeof(struct reg_t) == 8);
  long* sp = cpu->reg[SP].p;
  *--sp = cpu->pc;
  *--sp = cpu->state.fcsr_v;
  sp -= 64;			/* registers */
  memcpy(sp, cpu->reg, 64*sizeof(struct reg_t));
  cpu->reg[SP].p = sp;
  /* Call guest sa_handler(signum) */
  cpu->pc = guest_handler[cpu-core];
  cpu->reg[0].l = signum;
  fast_sim(cpu);
  fprintf(stderr, "sigreturn called\n");
  /* Pop registers, then fcsr, finally PC */
  assert(cpu->reg[SP].p == sp);
  //  long* sp = cpu->reg[SP].p;
  memcpy(cpu->reg, sp, 64*sizeof(struct reg_t));
  sp += 64;			/* registers */
  cpu->state.fcsr_v = *sp++;
  cpu->pc = *sp++;
  cpu->reg[SP].p = sp;
}  



static Addr_t emulate_brk(Addr_t addr, struct pinfo_t* info)
{
  Addr_t newbrk = addr;
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

int proxy_ecall( struct core_t* cpu )
{
  cpu->perf.ecalls++;
  long rvnum = cpu->reg[17].l;
  if (rvnum < 0 || rvnum >= rv_syscall_entries) {
  no_mapping:
    fprintf(stderr, "RISC-V system call %ld has no mapping to host system\n", rvnum);
    fprintf(stderr, "Arguments(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
            cpu->reg[10].l, cpu->reg[11].l, cpu->reg[12].l, cpu->reg[13].l, cpu->reg[14].l, cpu->reg[15].l);
    abort();
  }
  long sysnum = rv_to_host[rvnum].sysnum;
  if (conf.ecalls) {
    fprintf(stderr, "ecall %s->%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)", rv_to_host[rvnum].name, sysnum,
            cpu->reg[10].l, cpu->reg[11].l, cpu->reg[12].l, cpu->reg[13].l, cpu->reg[14].l, cpu->reg[15].l);
  }
  switch (sysnum) {
  case -1:
    goto no_mapping;
  case -2:
    fprintf(stderr, "RISCV-V system call %s(#%ld) not supported on host system\n", rv_to_host[rvnum].name, sysnum);
    abort();
    
#if 0
  case __NR_brk:
    cpu->reg[10].l = emulate_brk(cpu->reg[10].l, &current);
    break;
#endif

  case __NR_exit:
  case __NR_exit_group:
    return 1;

  case __NR_rt_sigaction:
    {
      fprintf(stderr, "rt_sigaction called\n");
      void (*action)(int) = cpu->reg[12].p;
      if (action != SIG_DFL && action != SIG_IGN)
	action = proxy_sa_handler;
      long signum = cpu->reg[10].l;
      struct sigaction* oldact = (struct sigaction*)cpu->reg[12].p;
      /* remember current guest handler */
      Addr_t old_handler = guest_handler[signum];
      /* make a copy of action structure */
      struct sigaction act;
      memcpy(&act, cpu->reg[11].p, sizeof(struct sigaction));
      /* then replace handler with proxy */
      long previous_guest_handler = guest_handler[signum];
      guest_handler[signum] = (Addr_t)act.sa_handler;
      act.sa_handler = action;	/* proxy handler or SIG_DFL, SIG_IGN */
      cpu->reg[10].l = syscall(__NR_rt_sigaction, signum, &act, oldact);
      /* swap back the previous guest handler */
      if (oldact)
	oldact->sa_handler = (void*)previous_guest_handler;
    }
    break;

#if 0
  case __NR_rt_sigprocmask:
    fprintf(stderr, "Trying to call rt_sigprocmask, always succeed without error.\n");
    cpu->reg[10].l = 0;  // always succeed without error
    break;
#endif

  case __NR_clone: /* sys_clone */
    parent_func(cpu);
    break;

  case __NR_times:
    {
      long count = (perf.h && conf.mhz) ? cpu->perf.cycles_simulated : cpu->perf.insn_executed;
      long denominator = conf.mhz ? conf.mhz*1000000 : 1000000000;
      count = (double)count * sysconf(_SC_CLK_TCK) / denominator;
      struct tms tms;
      memset(&tms, 0, sizeof tms);
      tms.tms_utime = count;
      memcpy(cpu->reg[10].p, &tms, sizeof tms);
      cpu->reg[10].l = 0;
    }
    break;

  case __NR_gettimeofday:
    { 
      long count = (perf.h && conf.mhz) ? cpu->perf.cycles_simulated : cpu->perf.insn_executed;
      long denominator = conf.mhz ? conf.mhz*1000000 : 1000000000;
      struct timeval tv;
      tv.tv_sec  = count / denominator;
      tv.tv_usec = count % denominator;
      tv.tv_sec  += cpu->perf.start_timeval.tv_sec;
      tv.tv_usec += cpu->perf.start_timeval.tv_usec;
      tv.tv_sec  += tv.tv_usec / 1000000;  // microseconds overflow
      tv.tv_usec %=              1000000;
      memcpy(cpu->reg[10].p, &tv, sizeof tv);
      cpu->reg[10].l = 0;
    }
    break;

  case __NR_close:
    if (cpu->reg[10].l <= 2) { // Don't close stdin, stdout, stderr
      cpu->reg[10].l = 0;
      break;
    }
    goto default_case;

  default:
  default_case:
    cpu->reg[10].l = syscall(sysnum, cpu->reg[10].l, cpu->reg[11].l, cpu->reg[12].l, cpu->reg[13].l, cpu->reg[14].l, cpu->reg[15].l);
    break;
  }
  if (conf.ecalls) {
    pid_t tid = syscall(SYS_gettid);
    fprintf(stderr, " return %lx pid=%d, tid=%d\n", cpu->reg[10].l, getpid(), tid);
  }
  return 0;
}


static void set_csr( struct core_t* cpu, int which, long val )
{
  switch (which) {
  case CSR_USTATUS:
    cpu->state.ustatus = val;
    return;
  case CSR_FFLAGS:
    cpu->state.fcsr.flags = val;
#ifdef SOFT_FP
    softfloat_exceptionFlags = val;
#else
#endif
    return;
  case CSR_FRM:
    cpu->state.fcsr.rmode = val;
    break;
  case CSR_FCSR:
    cpu->state.fcsr_v = val;
    break;
  default:
    fprintf(stderr, "Unsupported set_csr(%d, val=%lx)\n", which, val);
    abort();
  }
#ifdef SOFT_FP
  softfloat_roundingMode = cpu->state.fcsr.rmode;
#else
  fesetround(riscv_to_c_rm(cpu->state.fcsr.rmode));
#endif
}

static long get_csr( struct core_t* cpu, int which )
{
  switch (which) {
    case CSR_USTATUS:
    return cpu->state.ustatus;
  case CSR_FFLAGS:
#ifdef SOFT_FP
    cpu->state.fcsr.flags = softfloat_exceptionFlags;
#else
#endif
    return cpu->state.fcsr.flags;
  case CSR_FRM:
    return cpu->state.fcsr.rmode;
  case CSR_FCSR:
    return cpu->state.fcsr_v;
  default:
    fprintf(stderr, "Unsupported get_csr(%d)\n", which);
    abort();
  }
}

void proxy_csr( struct core_t* cpu, const struct insn_t* p, int which )
{
  enum Opcode_t op = p->op_code;
  int regop = op==Op_csrrw || op==Op_csrrs || op==Op_csrrc;
  long old_val = 0;
  long value = regop ? p->op_rs1 : p->op_constant>>12;
  if (op==Op_csrrw || op==Op_csrrwi) {
    if (p->op_rd != 0)
      old_val = get_csr(cpu, which);
    set_csr(cpu, which, value);
  }
  else {
    old_val = get_csr(cpu, which);
    if (regop || value != 0) {
      if (op==Op_csrrs || op==Op_csrrsi)
	value = old_val |  value;
      else
	value = old_val & ~value;
      set_csr(cpu, which, value);
    }
  }
  cpu->reg[p->op_rd].l = old_val;
}



int clone(int (*fn)(void *arg), void *child_stack, int flags, void *arg,
	  void *parent_tidptr, void *tls, void *child_tidptr);

#define _GNU_SOURCE
#include <linux/sched.h>

void parent_func(struct core_t* parent)
{
  dieif(active_cores >= conf.cores, "Too many clone system calls!");
  struct core_t* child = core + active_cores++;
  if (conf.simulate)
    perf.h->active = active_cores;
  init_core(child, parent, parent->pc+4, parent->reg[11].l, parent->reg[13].l);
#define STACK_SIZE (1L<<16)
  char* caveat_stack = malloc(STACK_SIZE);
  /*
    RISC-V clone system call arguments not same as wrapper or X86_64:
    a0 = flags
    a1 = child_stack
    a2 = parent_tidptr
    a3 = tls
    a4 = child_tidptr
  */
  parent->reg[10].l =
    clone(child_func,			     /* fn */
	  caveat_stack+STACK_SIZE,	     /* not stack of guest! */
	  parent->reg[10].l & ~CLONE_SETTLS, /* flags, but no host tls for now */
	  child,			     /* arg */
	  parent->reg[12].p,		     /* parent_tidptr of guest */
	  0,				     /* tls, but none for now */
	  parent->reg[14].p);		     /* child_tidptr of guest */
  /* returns child tid in guest a0 */
}
    
int child_func(void* arg)
{
  struct core_t* cpu = (struct core_t*)arg;
  /* child core is copy of parent core but with proper PC, SP and TP */
  cpu->tid = syscall(SYS_gettid);
  cpu->reg[10].l = 0;	       /* child a0==0 indicating I am child */
  fast_sim(cpu);
  fprintf(stderr, "child %d about to exit\n", cpu->tid);
  return 0;
}


