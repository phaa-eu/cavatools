/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "uspike.h"
#include "interpreter.h"

#define THREAD_STACK_SIZE  (1<<14)

struct syscall_map_t {
  int sysnum;
  const char* name;
};

struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"  
};
const int highest_ecall_num = HIGHEST_ECALL_NUM;

void status_report()
{
  long total = 0;
  double realtime = elapse_time();
  int n = 0;
  for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
    total += p->insn_count;
    n++;
  }
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", total, realtime, total/1e6/realtime);
  if (n <= 16) {
    char separator = '(';
    for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%1ld%%", separator, 100*p->insn_count/total);
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (n > 1)
    fprintf(stderr, "(%d cores)", n);
}

/* RISCV-V clone() system call arguments not same as X86_64:
   a0 = flags
   a1 = child_stack
   a2 = parent_tidptr
   a3 = tls
   a4 = child_tidptr
*/

static int thread_interpreter(void* arg)
{
  processor_t* p = (processor_t*)arg;
  WRITE_REG(2, READ_REG(11));	// a1 = child_stack
  WRITE_REG(4, READ_REG(13));	// a3 = tls
  WRITE_REG(10, 0);		// indicating we are child thread
  STATE.pc += 4;		// skip over ecall pc
  cpu_t* newcpu = new cpu_t(p);
  enum stop_reason reason;
  //conf.show = true;
  //  sleep(100);
  //fprintf(stderr, "starting thread interpreter, tid=%d, tp=%lx\n", gettid(), READ_REG(4));
  do {
    reason = interpreter(newcpu, conf.stat*1000000);
    status_report();
  } while (reason == stop_normal);
  status_report();
  fprintf(stderr, "\n");
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  else if (reason != stop_exited)
    die("unknown reason %d", reason);
  return 0;
	      
}

static bool proxy_ecall(processor_t* p, long executed)
{
  static long ecall_count;
  long rvnum = READ_REG(17);
  if (rvnum<0 || rvnum>highest_ecall_num || !rv_to_host[rvnum].name) {
    fprintf(stderr, "Illegal ecall number %ld\n", rvnum);
    abort();
  }
  long sysnum = rv_to_host[rvnum].sysnum;
  long a0=READ_REG(10), a1=READ_REG(11), a2=READ_REG(12), a3=READ_REG(13), a4=READ_REG(14), a5=READ_REG(15);
  const char* name = rv_to_host[rvnum].name;
  ecall_count++;
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "\n%12ld %8lx: ecalls=%ld %s:%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)",
	    executed, STATE.pc, ecall_count, name, sysnum, a0, a1, a2, a3, a4, a5);
  switch (sysnum) {
  case SYS_exit:
  case SYS_exit_group:
    return true;
#if 0
  case SYS_brk:
    {
      brk_lock.lock();
      long rv = emulate_brk(a0);
      brk_lock.unlock();
      return rv;
    }
#endif
  case SYS_clone:
    {
      //fprintf(stderr, "\nclone() called, tid=%d\n", gettid());
      processor_t* q = new processor_t(conf.isa, "mu", conf.vec, 0, 0, false, stdout);
      memcpy(q->get_state(), p->get_state(), sizeof(state_t));
      //memcpy(q, p, sizeof(processor_t));
      char* interp_stack = new char[THREAD_STACK_SIZE];
      interp_stack += THREAD_STACK_SIZE; // grows down
      long child_tid = proxy_clone(thread_interpreter, interp_stack, a0, q, (void*)a2, (void*)a4);
      WRITE_REG(10, (long)child_tid);
      //sleep(3);
      //fprintf(stderr, "returning from ecall, a0=%ld\n", READ_REG(10));
      //conf.show = true;
      goto finished;
    }
#if 0
  case SYS_futex:
    {
      timespec nap = { 0, 1000 }; // sleep for this many nanoseconds initially
      long counter = 1;
      long rc = proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5);
      while (rc == -1 && errno == EAGAIN && --counter >= 0) {
	nanosleep(&nap, 0);
	nap.tv_nsec *= 2;
	rc = proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5);
      }
      if (rc == -1)
	perror("Proxy futex");
      WRITE_REG(10, rc);
      goto finished;
    }
#endif
  }
  WRITE_REG(10, proxy_syscall(sysnum, executed, name, a0, a1, a2, a3, a4, a5));
 finished:
  if (conf.ecall && ecall_count % conf.ecall == 0)
    fprintf(stderr, "->0x%lx", READ_REG(10));
  return false;
}

cpu_t* initial_cpu(long entry, long sp)
{
  processor_t* p = new processor_t(conf.isa, "mu", conf.vec, 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  STATE.pc = entry;
  WRITE_REG(2, sp);
  cpu_t* mycpu = new cpu_t(p);
  return mycpu;
}

long cpu_t::get_pc()
{
  processor_t* p = spike_cpu;
  return STATE.pc;
}

long cpu_t::get_reg(int rn)
{
  processor_t* p = spike_cpu;
  return STATE.XPR[rn];
}

cpu_t* cpu_t::find(int tid)
{
  for (cpu_t* p=cpu_list; p; p=p->link)
    if (p->my_tid == tid)
      return p;
  return 0;
}

void cpu_t::show(long pc, FILE* f)
{
  Insn_t i = code.at(pc);
  int rn = i.op_rd==NOREG ? i.op.rs2 : i.op_rd;
  long rv = get_reg(rn);
  if (rn != NOREG)
    fprintf(stderr, "%6ld: %4s[%16lx] ", tid(), reg_name[rn], rv);
  else
    fprintf(stderr, "%6ld: %4s[%16s] ", tid(), "", "");
  labelpc(pc);
  disasm(pc);
}

Mutex_t lrsc_lock;
int tid_alone;

#define load_reserve(type) {					\
  long addr = READ_REG(i.op_rs1);				\
  long a = (cpu->tid() << 48) | (addr & 0x0000ffffffffffff);	\
  /*lrsc_lock.lock();*/						\
  tid_alone = cpu->tid();					\
  cpu->reserve_addr = a;					\
  /*long b = __sync_lock_test_and_set(&cpu->reserve_addr, a);*/	\
  WRITE_REG(i.op_rd, MMU.load_##type(addr));			\
  /*lrsc_lock.unlock();*/					\
  pc += 4;							\
}

#define store_conditional(type) {				\
  long addr = READ_REG(i.op_rs1);				\
  long a = (cpu->tid() << 48) | (addr & 0x0000ffffffffffff);	\
  /*lrsc_lock.lock();*/						\
  /*long b = __sync_lock_test_and_set(&cpu->reserve_addr, a);*/	\
  long b = cpu->reserve_addr;					\
  long failure = (b != a);					\
  if (!failure)							\
    MMU.store_int32(addr, READ_REG(i.op.rs2));			\
  cpu->reserve_addr = 0;					\
  /*lrsc_lock.unlock();*/					\
  tid_alone = 0;						\
  syscall(SYS_futex, (int*)&tid_alone, FUTEX_WAKE, 1, 0, 0, 0);	\
  WRITE_REG(i.op_rd, failure);					\
  pc += 4;							\
  if (failure) fprintf(stderr, "tid=%ld sc(%lx, %ld) %s\n", cpu->tid(), addr, READ_REG(i.op.rs2), failure?"failed":"succeeded"); \
}

template<class T> bool cmpswap(long pc, processor_t* p)
{
  Insn_t i = code.at(pc);
  T* ptr = (T*)READ_REG(i.op_rs1);
  T expect  = READ_REG(i.op.rs2);
  T replace = READ_REG(i.op.rs3);
  T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
  WRITE_REG(code.at(pc+4).op_rs1, oldval);
  if (oldval == expect)  WRITE_REG(i.op_rd, 0);	/* sc was successful */
  return oldval == expect;
}
  
union frf_t{
  long l[2];
  double d;
  float s;
  char pad[16];
  void operator=(long   x) { l[1]=~0L; l[0]=x; }
  void operator=(double x) { l[1]=~0L; d=x; }
  void operator=(float  x) { l[1]=l[0]=~0L; s=x; }
};

#define immed i.op.imm
#define longimm i.op_immed
//#define r1 (int64_t)(p->get_state()->XPR[i.op_rs1])
//#define r2 (int64_t)(p->get_state()->XPR[i.op.rs2])
//#define wrd(e) p->get_state()->XPR.write(i.op_rd, e)
#define wpc(e) pc=(e)
#define r1 xrf[i.op_rs1]
#define r2 xrf[i.op.rs2]
#define wrd(e) xrf[i.op_rd]=(e)

union conv_t { float32_t sf; float hf; float64_t sd; double hd; };
inline float  float_of( float32_t x) { conv_t c; c.sf=x; return c.hf; }
inline double double_of(float64_t x) { conv_t c; c.sd=x; return c.hd; }
#define f1 float_of(f32(READ_FREG(i.op_rs1)))
#define f2 float_of(f32(READ_FREG(i.op.rs2)))
#define f3 float_of(f32(READ_FREG(i.op.rs3)))
#define d1 double_of(f64(READ_FREG(i.op_rs1)))
#define d2 double_of(f64(READ_FREG(i.op.rs2)))
#define d3 double_of(f64(READ_FREG(i.op.rs3)))
#define wfrd(x) { conv_t c; c.hf=x; DO_WRITE_FREG(i.op_rd, freg(c.sf)); }
#define wdrd(x) { conv_t c; c.hd=x; DO_WRITE_FREG(i.op_rd, freg(c.sd)); }

enum stop_reason interpreter(cpu_t* cpu, long number)
{
  //fprintf(stderr, "interpreter()\n");
  processor_t* p = cpu->spike();
  long* xrf = (long*)&p->get_state()->XPR;
  
  enum stop_reason reason = stop_normal;
  long pc = STATE.pc;
  long count = 0;
  while (count < number) {
    do {
      
#if 0
      int v;
      if ((v=tid_alone) && (v!=cpu->tid())) {
	fprintf(stderr, "tid_alone=%d, my_tid=%ld waiting\n", v, cpu->tid());
	while ((v=tid_alone) && (v!=cpu->tid())) {
	  syscall(SYS_futex, (int*)&tid_alone, FUTEX_WAIT, v, 0, 0, 0);
	  fprintf(stderr, "still waiting\n");
	}
      }
#endif
      
#ifdef DEBUG
      long oldpc = pc;
      cpu->debug.insert(cpu->insn_count+count+1, pc);
#endif
      if (!code.valid(pc))
	diesegv();
    repeat_dispatch:
      Insn_t i = code.at(pc);
      switch (i.op_code) {
      case Op_ZERO:
	code.set(pc, decoder(code.image(pc), pc));
	goto repeat_dispatch;
	
#include "fastops.h"
	
      case Op_ecall:
	STATE.pc=pc;
	if (proxy_ecall(p, cpu->insn_count+count)) {
	  reason = stop_exited;
	  goto early_stop;
	}
	pc += 4;
	break;
      case Op_ebreak:
      case Op_c_ebreak:
	reason = stop_breakpoint;
	goto early_stop;

      case Op_cas_w:
	pc += cmpswap<int32_t>(pc, p) ? 12 : i.op.imm;
	break;
      case Op_c_cas_w:
	pc += cmpswap<int32_t>(pc, p) ? 10 : i.op.imm;
	break;
      case Op_cas_d:
	pc += cmpswap<int64_t>(pc, p) ? 12 : i.op.imm;
	break;
      case Op_c_cas_d:
	pc += cmpswap<int64_t>(pc, p) ? 10 : i.op.imm;
	break;

      case Op_lr_w:
	load_reserve(int32);
	break;
      case Op_lr_d:
	load_reserve(int64);
	break;
      case Op_sc_w:
	store_conditional(int32);
	break;
      case Op_sc_d:
	store_conditional(int64);
	break;
	
      default:
	try {
	  pc = golden[i.op_code](pc, cpu);
	} catch (trap_user_ecall& e) {
	  //if (proxy_ecall(p, cpu->insn_count+count)) {
	  if (proxy_ecall(p, cpu->insn_count)) {
	    reason = stop_exited;
	    goto early_stop;
	  }
	  pc += 4;
	  if (conf.show) {
	    //count++;
	    cpu->insn_count++;
	    goto early_stop;
	  }
	} catch (trap_breakpoint& e) {
	  reason = stop_breakpoint;
	  goto early_stop;
	}
	break;
      }
      xrf[0] = 0;
      
#ifdef DEBUG
      i = code.at(oldpc);
      int rn = i.op_rd==NOREG ? i.op.rs2 : i.op_rd;
      cpu->debug.addval(i.op_rd, cpu->get_reg(rn));
      if (conf.show)
	cpu->show(oldpc);
#endif

    } while (++count < number);
  }
 early_stop:
  STATE.pc = pc;
  cpu->insn_count += count;
  return reason;
}
			  
long I_ZERO(long pc, cpu_t* cpu)    { die("I_ZERO should never be dispatched!"); }
long I_ILLEGAL(long pc, cpu_t* cpu) { die("I_ILLEGAL at 0x%lx", pc); }
long I_UNKNOWN(long pc, cpu_t* cpu) { die("I_UNKNOWN at 0x%lx", pc); }

//long I_ecall(long pc, cpu_t* cpu)   { die("I_ecall should never be dispatched!"); }
//long I_ebreak(long pc, cpu_t* cpu)   { die("I_ebreak should never be dispatched!"); }
//long I_c_ebreak(long pc, cpu_t* cpu)   { die("I_c_ebreak should never be dispatched!"); }

#include "dispatch_table.h"
