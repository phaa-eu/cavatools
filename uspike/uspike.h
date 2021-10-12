/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cassert>
#include <cstdint>
#include <stdio.h>
#include <string.h>

/*
  Utility stuff.
*/
#define diesegv()  (*(char*)0=1)
#define checkif(bad) if (!(bad)) diesegv()

#define die(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); diesegv(); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); diesegv(); }
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }

extern "C" {
  long load_elf_binary(const char* file_name, int include_data);
  int elf_find_symbol(const char* name, long* begin, long* end);
  const char* elf_find_pc(long pc, long* offset);
  long initialize_stack(int argc, const char** argv, const char** envp);
  long emulate_brk(long addr);
  extern unsigned long low_bound, high_bound;
  void redecode(long pc);
};

void status_report();
class hart_t* initial_cpu(long entry, long sp);
void show_insn(long pc, int tid);

static inline bool find_symbol(const char* name, long &begin, long &end) { return elf_find_symbol(name, &begin, &end) != 0; }
static inline const char* find_pc(long pc, long &offset) { return elf_find_pc(pc, &offset); }

void show(hart_t* cpu, long pc, FILE* f =stderr);
void start_time();
double elapse_time();
void status_report();
