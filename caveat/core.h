/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define futex(a, b, c, d)  syscall(SYS_futex, a, b, c, d, 0, 0)

class futex_mutex_t {
  int flag;
public:
  void lock() {
    if (__sync_bool_compare_and_swap(&flag, 0, 1))
      return;			// success, no one had lock
    // not zero, someone has lock
    do {
      // assume lock still taken, try make it 2 and wait
      if (flag==2 || __sync_bool_compare_and_swap(&flag, 1, 2))
	futex(&flag, FUTEX_WAIT, 2, 0);
      // try (again) assuming lock is free
    } while (!__sync_bool_compare_and_swap(&flag, 0, 2));
    // transition 0->2 successful
  }
  void unlock() {
    // flag must be either 1 or 2
    if (__sync_fetch_and_add(&flag, -1) == 2) {
      flag = 0;
      futex(&flag, FUTEX_WAKE, 1, 0);
    }
  }
};

class smp_t {
  futex_mutex_t *flag;		// mutual exclusion locks
  int lg_line;			// log-2 bytes per cache line
  long bus_mask;		// number of busses - 1
public:
  smp_t(int lg_linesize, int lg_busses =1);
  int bus(long addr) { return (addr >> lg_line) & bus_mask; }
  long acquire_bus(int b =0);
  void release_bus(int b =0);
  long read_line(long addr, int b =0)  { return 10; }
  long write_line(long addr, int b =0) { return 10; }
};

extern smp_t* smp;

class core_t : public hart_t {
  static volatile long active_cores;
  static volatile long _system_clock; // global system cycle counter
  volatile long now;		     // local clock, >= system_clock
  long start_time;		     // system_clock when core allocated
  long stall_time;		     // cycles in syscall and shared bus

  long saved_local_time;
  long last_pc;
  
  perf_t perf;
  core_t();
  
public:
  core_t(long entry);
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }
  hart_t* newcore() { core_t* h=new core_t(); h->copy_state(this); h->last_pc=read_pc(); return h; }
  void proxy_syscall(long sysnum);
  int run_thread();

  static long system_clock() { return _system_clock; }
  void sync_system_clock();	     // wait until world catches up
  static void update_system_clock(); // called periodically
  static void print_status();	     // show on console

  fsm_cache<4, true, true> dc;
  
  void fetch_dcache(long addr, long pc)
  {
    if (dc.fetch(addr)) {
      int b = smp->bus(addr);
      long stalled = smp->acquire_bus(b);
      stalled += smp->read_line(addr, b);
      smp->release_bus(b);
      perf.inc_dmiss(pc);
      perf.inc_cycle(pc, stalled);
      now += stalled;
    }
  }

  void update_dcache(long addr, long pc)
  {
    long eviction = 0;
    //if (dc.update(a, eviction)) {
    if (dc.update(addr)) {
      long stalled = 0;
      if (eviction) {
	int b = smp->bus(eviction);
	stalled += smp->acquire_bus(b);
	smp->write_line(eviction, b); // don't care about latency
	smp->release_bus(b);
      }
      int b = smp->bus(addr);
      stalled += smp->acquire_bus(b);
      stalled += smp->read_line(addr, b);
      smp->release_bus(b);
      perf.inc_dmiss(pc);
      perf.inc_cycle(pc, stalled);
      now += stalled;
    }
  }

  long load_model( long a,  long pc) { fetch_dcache( a, pc); return a; }
  long store_model(long a,  long pc) { update_dcache(a, pc); return a; }
  void amo_model(  long a,  long pc) { update_dcache(a, pc);           }
  void custom(long pc) { }

  int number() { return perf.number(); }
  bool stalled() { return now == LONG_MAX; }
  long cycles() { return stalled() ? saved_local_time : now; }
  long stalls() { return stall_time; }
  long run_time() { return cycles() - start_time; }
  long run_cycles() { return run_time() - stall_time; }
};

