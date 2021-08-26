/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <cassert>
#include <cstdint>
#include <stdio.h>
#include <string.h>

using namespace std;
void* operator new(size_t size);
void operator delete(void*) noexcept;

/*
  Utility stuff.
*/
#define die(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); abort(); }
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }

#define diesegv()  (*(char*)0=1)
#define checkif(bad) if (!(bad)) diesegv()

extern "C" {
  long load_elf_binary(const char* file_name, int include_data);
  int elf_find_symbol(const char* name, long* begin, long* end);
  const char* elf_find_pc(long pc, long* offset);
  long initialize_stack(int argc, const char** argv, const char** envp, long entry);
  extern unsigned long low_bound, high_bound;
  void start_time(int mhz);
  double elapse_time();
  double simulated_time(long cycles);
  long emulate_brk(long addr);
  long proxy_syscall(long sysnum, long cycles, const char* name, long a0, long a1, long a2, long a3, long a4, long a5);
  int proxy_clone(int (*fn)(void*), void *interp_stack, int flags, void *arg, void *parent_tidptr, void *child_tidptr);

  struct configuration_t {
    const char* isa;
    const char* vec;
    int mhz;
    int stat;
    bool show;
    const char* gdb;
    int ecall;
  };
  extern configuration_t conf;
};

enum stop_reason { stop_normal, stop_exited, stop_breakpoint };

enum stop_reason interpreter(class cpu_t* mycpu, long number);
void status_report();
class cpu_t* initial_cpu(long entry, long sp);
void show_insn(long pc, int tid);

static inline bool find_symbol(const char* name, long &begin, long &end) { return elf_find_symbol(name, &begin, &end) != 0; }
static inline const char* find_pc(long pc, long &offset) { return elf_find_pc(pc, &offset); }

class insnSpace_t {
  long base;
  long limit;
  class Insn_t* predecoded;
public:  
  insnSpace_t() { base=limit=0; predecoded=0; }
  void init(long lo, long hi);
  bool valid(long pc) { return base<=pc && pc<limit; }
  long index(long pc) { checkif(valid(pc)); return (pc-base)/2; }
  Insn_t at(long pc) { return predecoded[index(pc)]; }
  uint32_t image(long pc) { checkif(valid(pc)); return *(uint32_t*)(pc); }
  Insn_t set(long pc, Insn_t i) { predecoded[index(pc)] = i; return i; }
};

extern insnSpace_t code;
extern const char* op_name[];
extern const char* reg_name[];

void labelpc(long pc, FILE* f =stderr);
void disasm(long pc, const char* end, FILE* f =stderr);
inline void disasm(long pc, FILE* f =stderr) { disasm(pc, "\n", f); }
void show(cpu_t* cpu, long pc, FILE* f =stderr);

void OpenTcpLink(const char* name);
void ProcessGdbCommand(void* spike_state =0);
void HandleException(int signum);
