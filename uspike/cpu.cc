#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "uspike.h"
#include "cpu.h"

cpu_t* cpu_t::cpu_list =0;
long cpu_t::reserve_addr =0;

cpu_t::cpu_t(processor_t* p)
{
  spike_cpu = p;
  my_tid = gettid();
  insn_count = 0;
  link = cpu_list;
  cpu_list = this;
}

void cpu_t::acquire_load_reservation(long a)
{
  a = (tid() << 48) | (a & 0x0000ffffffffffff);
  long b = __sync_lock_test_and_set(&reserve_addr, a);
  if (b)
    reserve_addr = 0;
}

void cpu_t::yield_load_reservation()
{
  reserve_addr = 0;
}

bool cpu_t::check_load_reservation(long a, size_t size)
{
  a = (tid() << 48) | (a & 0x0000ffffffffffff);
  return reserve_addr == a;
}

#ifdef DEBUG
pctrace_t Debug_t::get()
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  return trace[cursor];
}

void Debug_t::insert(pctrace_t pt)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor] = pt;
}

void Debug_t::insert(long c, long pc)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor].count = c;
  trace[cursor].pc    = pc;
  trace[cursor].val   = ~0l;
  trace[cursor].rn    = GPREG;
}

void Debug_t::addval(int rn, long val)
{
  trace[cursor].rn    = rn;
  trace[cursor].val   = val;
}

void Debug_t::print(FILE* f)
{
  for (int i=0; i<PCTRACEBUFSZ; i++) {
    pctrace_t t = get();
    if (t.rn != NOREG)
      fprintf(stderr, "%15ld %4s[%016lx] ", t.count, reg_name[t.rn], t.val);
    else
      fprintf(stderr, "%15ld %4s[%16s] ", t.count, "", "");
    labelpc(t.pc);
    if (code.valid(t.pc))
      disasm(t.pc, "");
    fprintf(stderr, "\n");
  }
}

void signal_handler(int nSIGnum, siginfo_t* si, void* vcontext)
{
  //  ucontext_t* context = (ucontext_t*)vcontext;
  //  context->uc_mcontext.gregs[]
  fprintf(stderr, "\n\nsignal_handler(%d)\n", nSIGnum);
  if (conf.gdb) {
    HandleException(nSIGnum);
    ProcessGdbCommand();
  }
  else
    cpu_t::find(gettid())->debug.print();
  exit(-1);
  //  longjmp(return_to_top_level, 1);
}
#endif

// An atomic_compare_exchange wrapper with semantics expected by the paper's
// mutex - return the old value stored in the atom.
inline int cmpxchg(std::atomic<int>* atom, int expected, int desired) {
  int* ep = &expected;
  std::atomic_compare_exchange_strong(atom, ep, desired);
  return *ep;
}

void Mutex_t::lock()
{
  int c = cmpxchg(&atom_, 0, 1);
  // If the lock was previously unlocked, there's nothing else for us to do.
  // Otherwise, we'll probably have to wait.
  if (c != 0) {
    do {
      // If the mutex is locked, we signal that we're waiting by setting the
      // atom to 2. A shortcut checks is it's 2 already and avoids the atomic
      // operation in this case.
      if (c == 2 || cmpxchg(&atom_, 1, 2) != 0) {
	// Here we have to actually sleep, because the mutex is actually
	// locked. Note that it's not necessary to loop around this syscall;
	// a spurious wakeup will do no harm since we only exit the do...while
	// loop when atom_ is indeed 0.
	syscall(SYS_futex, (int*)&atom_, FUTEX_WAIT, 2, 0, 0, 0);
      }
      // We're here when either:
      // (a) the mutex was in fact unlocked (by an intervening thread).
      // (b) we slept waiting for the atom and were awoken.
      //
      // So we try to lock the atom again. We set teh state to 2 because we
      // can't be certain there's no other thread at this exact point. So we
      // prefer to err on the safe side.
    } while ((c = cmpxchg(&atom_, 0, 2)) != 0);
  }
}

void Mutex_t::unlock()
{
  if (atom_.fetch_sub(1) != 1) {
    atom_.store(0);
    syscall(SYS_futex, (int*)&atom_, FUTEX_WAKE, 1, 0, 0, 0);
  }
}
