
#define RISCV_PGSHIFT 12
#define RISCV_PGSIZE (1 << RISCV_PGSHIFT)

#define ROUNDUP(a, b) ((((a)-1)/(b)+1)*(b))
#define ROUNDDOWN(a, b) ((a)/(b)*(b))

#ifdef __cplusplus
extern "C" {
#endif

/*  Process information  */
struct pinfo_t {
  long phnum;
  long phent;
  long phdr;
  long phdr_size;
  long entry;
  long stack_top;
  long brk;
  long brk_min;
  long brk_max;
};

extern struct pinfo_t current;
extern unsigned long low_bound, high_bound;

long load_elf_binary(const char* file_name, int include_data);
int elf_find_symbol(const char* name, long* begin, long* end);
const char* elf_find_pc(long pc, long* offset);

long initialize_stack(int argc, const char** argv, const char** envp);
long emulate_brk(long addr);

#ifdef __cplusplus
}
#endif
  
