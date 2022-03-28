/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cstdint>
#include <limits.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "uspike.h"
#include "options.h"
#include "mmu.h"
#include "hart.h"
#include "perf.h"
#include "../cache/cache.h"
#include "core.h"

volatile long core_t::active_cores;
volatile long core_t::_system_clock;

smp_t::smp_t(int lg_linesize, int lg_busses)
{
  lg_line = lg_linesize;
  bus_mask = (1<<lg_busses)-1;
  flag = new futex_mutex_t[1<<lg_busses];
}

long smp_t::acquire_bus(int b) {
  assert(0<=b && b<=bus_mask);
  long start = core_t::system_clock();
  flag[b].lock();
  return core_t::system_clock() - start; // elapse clock cycles
}

void smp_t::release_bus(int b) {
  assert(0<=b && b<=bus_mask);
  flag[b].unlock();
}


extern option<int> conf_Dline;
extern option<int> conf_Drows;
extern option<int> conf_Jump;

core_t::core_t() : perf(__sync_fetch_and_add(&active_cores, 1)),
		   dc(conf_Dline, conf_Drows, "Data")
{
  now = start_time = system_clock();
  stall_time = 0;
}

core_t::core_t(long entry) : core_t()
{
  last_pc = entry;
}

int core_t::run_thread()
{
  try {
    while (1) {
      volatile count_t* c = perf.count_ptr(read_pc());
      long end;
      now += interpreter(end);
      for (; c<perf.count_ptr(end); c++) {
	c->executed++;
	c->cycles++;
      }
      now += conf_Jump;		// taken branch penalty
      c->cycles += conf_Jump;
    }
  } catch (int return_code) {
    return return_code;
  }
}
  
// we use execution count -1 to denote stalled at this pc
void core_t::proxy_syscall(long sysnum)
{
  long pc = read_pc();
  long saved_count = perf.count(pc);
  perf.inc_count(pc, -saved_count-1);
  core_t::update_system_clock();
  sync_system_clock();
  long t0 = system_clock();
  saved_local_time = now;
  now = LONG_MAX;	// indicate we are stalled
  core_t::update_system_clock();
  hart_t::proxy_syscall(sysnum);
  now = system_clock();
  stall_time += now - t0; // accumulate stalled 
  core_t::update_system_clock();
  perf.inc_count(pc, saved_count+1);
}

#undef  futex
#define futex(a, b, c, d)  syscall(SYS_futex, a, b, c, d, 0, 0)

void core_t::update_system_clock()
{
  long last_local = LONG_MAX;
  for (core_t* p=core_t::list(); p; p=p->next()) {
    if (p->now < last_local)
      last_local = p->now;
  }
  if (last_local > _system_clock && last_local < LONG_MAX) {
    _system_clock = last_local;
    futex((int*)&_system_clock, FUTEX_WAKE, INT_MAX, 0);
  }
}

void core_t::sync_system_clock()
{
  static struct timespec timeout = { 0, 10000 };
  update_system_clock();
  long t = system_clock();
  while (t < now) {
    futex((int*)&_system_clock, FUTEX_WAIT, (int)t, &timeout);
    t = system_clock();
  }
}

void core_t::print_status()
{
  update_system_clock();
  long now = system_clock();
  double elapse_time();
  double realtime = elapse_time();
  long threads = 0;
  long tInsns = 0;
  double aUtil = 0.0;
  for (core_t* p=core_t::list(); p; p=p->next()) {
    threads++;
    tInsns += p->executed();
    aUtil += (double)p->run_cycles()/p->run_time();
  }
  aUtil /= threads;
  char buf[4096];
  char* b = buf;
  b += sprintf(b, "\r\33[2K%12ld insns %3.1fs MCPS=%3.1f MIPS=%3.1f(%3.1f%%) IPC(mpki,u)",
	       tInsns, realtime, (double)now/1e6/realtime, tInsns/1e6/realtime, 100.0*aUtil);
  char separator = '=';
  for (core_t* p=core_t::list(); p; p=p->next()) {
    double ipc = (double)p->executed()/p->run_cycles();
    long dmpk = 1000.0*p->dc.misses()/p->executed() + 0.5;
    long util = 100.0*p->run_cycles()/p->run_time() + 0.5;
    b += sprintf(b, "%c%4.2f(%ld,%ld%%)", separator, ipc, dmpk, util);
    separator = ',';
  }
  fputs(buf, stderr);
}

