#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "uspike.h"
#include "mmu.h"

long mmu_t::reserve_addr = ~0L;

mmu_t::mmu_t()
{
}

void mmu_t::acquire_load_reservation(long a)
{
  //  fprintf(stderr, "acquire_load_reservation(%lx)\n", a);
  long key = ((long)gettid() << 48) | (a & 0x0000ffffffffffff);
  reserve_addr = key;
}

void mmu_t::yield_load_reservation()
{
  //  fprintf(stderr, "yield_load_reservation()\n");
  reserve_addr = ~0L;
}

bool mmu_t::check_load_reservation(long a, size_t size)
{
  long key = ((long)gettid() << 48) | (a & 0x0000ffffffffffff);
  //  fprintf(stderr, "check_load_reservation(%lx)=%d\n", a, key==reserve_addr);
  return key == reserve_addr;
}

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
