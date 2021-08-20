#include <unistd.h>
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
