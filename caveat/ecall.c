#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <math.h>
#include <string.h>

//#include "encoding.h"

//#define NO_FP_MACROS
#include "caveat_fp.h"
#include "arith.h"

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"
#include "shmfifo.h"
#include "core.h"
#include "riscv-opc.h"

#define __NR_arch_specific_syscall	244	/* in RISC-V but not X86_64 */
#define __NR_syscalls			436	/* in RISC-V but not X86_64 */
#define __NR_getmainvars		436	/* in RISC-V but not X86_64 */

#include "ecall_nums.h"

#define DEBUG


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
  static long previous =0;
  assert(insn(cpu->pc)->op_code == Op_ecall);
  long rvnum = cpu->reg[17].l;
  if (rvnum < 0 || rvnum >= rv_syscall_entries) {
    fprintf(stderr, "%ld out of range (%lx, %lx, %lx, %lx, %lx, %lx)\n",
            rvnum, cpu->reg[10].l, cpu->reg[11].l, cpu->reg[12].l, cpu->reg[13].l, cpu->reg[14].l, cpu->reg[15].l);
    abort();
  }
  long x86num = rv_to_x64_syscall[rvnum].x64num;
#ifdef DEBUG
  fprintf(stderr, "%10ld: %s[%ld:%ld](%lx, %lx, %lx, %lx, %lx, %lx)", cpu->counter.insn_executed-previous,
	  rv_to_x64_syscall[rvnum].name, rvnum, x86num,
	  cpu->reg[10].l, cpu->reg[11].l, cpu->reg[12].l, cpu->reg[13].l, cpu->reg[14].l, cpu->reg[15].l);
  previous = cpu->counter.insn_executed;
#endif
  switch (x86num) {

  case 12:  /* sys_brk */
    cpu->reg[10].l = emulate_brk(cpu->reg[10].l, &current);
    break;

  case 60:  /* sys_exit */
  case 231: /* sys_exit_group */\
    return 1;

  case 13: /* sys_rt_sigaction */
    fprintf(stderr, "Trying to call rt_sigaction, always succeed without error.\n");
    cpu->reg[10].l = 0;  // always succeed without error
    break;

  case 56: /* sys_clone */
    abort();

  case 96:  /* gettimeofday */
#define PRETEND_MIPS 1000
#ifdef PRETEND_MIPS
    {
      struct timeval tv;
      tv.tv_sec  = (cpu->counter.insn_executed / PRETEND_MIPS) / 1000000;
      tv.tv_usec = (cpu->counter.insn_executed / PRETEND_MIPS) % 1000000;
      tv.tv_sec  += cpu->counter.start_timeval.tv_sec;
      tv.tv_usec += cpu->counter.start_timeval.tv_usec;
      tv.tv_sec  += tv.tv_usec / 1000000;  // microseconds overflow
      tv.tv_usec %=              1000000;
      //      fprintf(stderr, "gettimeofday(sec=%ld, usec=%4ld)\n", tv.tv_sec, tv.tv_usec);
      memcpy(cpu->reg[10].p, &tv, sizeof tv);
      cpu->reg[10].l = 0;
    }
    break;
#else
    goto default_case;
#endif

  case 3: /* sys_close */
    if (cpu->reg[10].l <= 2) { // Don't close stdin, stdout, stderr
      cpu->reg[10].l = 0;
      break;
    }
    goto default_case;

  default:
  default_case:
    cpu->reg[10].l = syscall(x86num, cpu->reg[10].l, cpu->reg[11].l, cpu->reg[12].l, cpu->reg[13].l, cpu->reg[14].l, cpu->reg[15].l);
    break;
  }
#ifdef DEBUG
  fprintf(stderr, " return %lx\n", cpu->reg[10].l);
#endif
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
