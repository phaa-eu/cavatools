#include <stdint.h>
#include <stdio.h>

#include "cache.h"
#include "../uspike/options.h"
#include "../uspike/mmu.h"
#include "../uspike/cpu.h"

class mem_t : public mmu_t {
  long load_model( long a, int b) { dc->lookup(a);       return a; }
  long store_model(long a, int b) { dc->lookup(a, true); return a; }
  void amo_model(  long a, int b) { dc->lookup(a, true); }
public:
  cache_t* dc;
  mem_t(cache_t* data) { dc=data; }
};

void start_time();
double elapse_time();
void interpreter(cpu_t* cpu);
void status_report();

cache_t* dc;

/*void status_report
  double realtime = elapse_time();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", cpu_t::total_count(), realtime, cpu_t::total_count()/1e6/realtime);
  if (cpu_t::threads() <= 16) {
    char separator = '(';
    for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%1ld%%", separator, 100*p->count()/cpu_t::total_count());
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (cpu_t::threads() > 1)
    fprintf(stderr, "(%d cores)", cpu_t::threads());
*/

void exitfunc()
{
  fprintf(stderr, "\n\n");
  for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
    mem_t* mm = (mem_t*)p->mmu();
    fprintf(stderr, "Core [%ld] ", p->tid());
    mm->dc->print();
  }
  fprintf(stderr, "\n\n");
  status_report();
}

int main(int argc, const char* argv[], const char* envp[])
{
  parse_options(argc, argv, "uspike: user-mode RISC-V interpreter derived from Spike");
  if (argc == 0)
    help_exit();
  start_time();
  dc = new cache_t("Data Cache", 20, 4, 6, 6, true);
  mem_t* mm = new mem_t(dc);
  cpu_t* mycpu = new cpu_t(argc, argv, envp, mm);
  atexit(exitfunc);
  while (1) {
    mycpu->run_epoch(100000000L);
    double realtime = elapse_time();
    fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS D$=", cpu_t::total_count(), realtime, cpu_t::total_count()/1e6/realtime);
    char separator = '=';
    for (cpu_t* p=cpu_t::list(); p; p=p->next()) {
      mem_t* mm = (mem_t*)p->mmu();
      fprintf(stderr, "%c%4.2f%%", separator, 100.0*mm->dc->misses()/mm->dc->refs());
    }
  }
}
