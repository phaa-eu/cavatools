
#define RISCV_PGSHIFT 12
#define RISCV_PGSIZE (1 << RISCV_PGSHIFT)

#define ROUNDUP(a, b) ((((a)-1)/(b)+1)*(b))
#define ROUNDDOWN(a, b) ((a)/(b)*(b))

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
