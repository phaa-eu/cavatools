/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <elf.h>

#include "processinfo.h"

/*
  Utility stuff.
*/
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }

#define MEM_END		0x60000000L
#define STACK_SIZE	0x01000000L
#define BRK_SIZE	0x01000000L

struct pinfo_t current;
unsigned long low_bound, high_bound;

static long phdrs[128];

static char* strtbl;
static Elf64_Sym* symtbl;
static long num_syms;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(a, lo, hi) MIN(MAX(a, lo), hi)

/**
 * Get an annoymous memory segment using mmap() and load
 * from file at offset.  Return 0 if fail.
 */
static void* load_elf_section(int file, ssize_t offset, ssize_t size)
{
  void* where = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (where == 0)
    return 0;
  ssize_t ret = lseek(file, offset, SEEK_SET);
  if (ret < 0)
    return 0;
  ret = read(file, where, size);
  if (ret < size)
    return 0;
  return where;
}

/**
 * The protection flags are in the p_flags section of the program header.
 * But rather annoyingly, they are the reverse of what mmap expects.
 */
static inline int get_prot(uint32_t p_flags)
{
  int prot_x = (p_flags & PF_X) ? PROT_EXEC  : PROT_NONE;
  int prot_w = (p_flags & PF_W) ? PROT_WRITE : PROT_NONE;
  int prot_r = (p_flags & PF_R) ? PROT_READ  : PROT_NONE;
  return (prot_x | prot_w | prot_r);
}


long load_elf_binary( const char* file_name, int include_data )
/* file_name	- name of ELF binary, must be statically linked for now
   include_data	- 1=load DATA and BSS segments, 0=load TEXT only
   returns entry point address */
{
  current.phdr = (uint64_t)phdrs;
  current.phdr_size = sizeof(phdrs);
  struct pinfo_t* info = &current;
  int flags = MAP_FIXED | MAP_PRIVATE;
  ssize_t ehdr_size;
  size_t phdr_size;
  long number_of_insn;
  long stack_lowest;
  size_t tblsz;
  char* shstrtbl;
  ssize_t ret;

  int file = open(file_name, O_RDONLY, 0);
  quitif(file<0, "Unable to open binary file \"%s\"\n", file_name);

  Elf64_Ehdr eh;
  ehdr_size = read(file, &eh, sizeof(eh));
  quitif(ehdr_size < (ssize_t)sizeof(eh) ||
	 !(eh.e_ident[0] == '\177' && eh.e_ident[1] == 'E' &&
	   eh.e_ident[2] == 'L'    && eh.e_ident[3] == 'F'),
	 "Elf header not correct");
  phdr_size = eh.e_phnum * sizeof(Elf64_Phdr);
  quitif(phdr_size > info->phdr_size, "Phdr too big");

  dieif(lseek(file, eh.e_shoff, SEEK_SET) < 0, "lseek failed");
  dieif(read(file, (void*)info->phdr, phdr_size) != (ssize_t)phdr_size, "read(phdr) failed");
  info->phnum = eh.e_phnum;
  info->phent = sizeof(Elf64_Phdr);
  Elf64_Phdr* ph = (Elf64_Phdr*)load_elf_section(file, eh.e_phoff, phdr_size);
  dieif(ph==0, "cannot load phdr");
  info->phdr = (size_t)ph;

  // don't load dynamic linker at 0, else we can't catch NULL pointer derefs
  uintptr_t bias = 0;
  if (eh.e_type == ET_DYN)
    bias = RISCV_PGSIZE;

  info->entry = eh.e_entry + bias;
  for (int i = eh.e_phnum - 1; i >= 0; i--) {
    //fprintf(stderr, "section %d p_vaddr=0x%lx p_memsz=0x%lx\n", i, ph[i].p_vaddr, ph[i].p_memsz);
    quitif(ph[i].p_type==PT_INTERP, "Not a statically linked ELF program");
    if(ph[i].p_type == PT_LOAD && ph[i].p_memsz) {
      //fprintf(stderr, "  loaded\n");
      uintptr_t prepad = ph[i].p_vaddr % RISCV_PGSIZE;
      uintptr_t vaddr = ph[i].p_vaddr + bias;
      if (vaddr + ph[i].p_memsz > info->brk_min)
        info->brk_min = vaddr + ph[i].p_memsz;
      int flags2 = flags | (prepad ? MAP_POPULATE : 0);
      int prot = get_prot(ph[i].p_flags);
      void* rc = mmap((void*)(vaddr-prepad), ph[i].p_filesz + prepad, prot | PROT_WRITE, flags2, file, ph[i].p_offset - prepad);
      dieif(rc != (void*)(vaddr-prepad), "mmap(0x%ld) returned %p\n", (vaddr-prepad), rc);
      memset((void*)(vaddr-prepad), 0, prepad);
      if (!(prot & PROT_WRITE))
        dieif(mprotect((void*)(vaddr-prepad), ph[i].p_filesz + prepad, prot), "Could not mprotect()\n");
      size_t mapped = ROUNDUP(ph[i].p_filesz + prepad, RISCV_PGSIZE) - prepad;
      if (ph[i].p_memsz > mapped)
        dieif(mmap((void*)(vaddr+mapped), ph[i].p_memsz - mapped, prot, flags|MAP_ANONYMOUS, 0, 0) != (void*)(vaddr+mapped), "Could not mmap()\n");      
    }
    info->brk_max = info->brk_min + BRK_SIZE;
  }

  /* Read section header string table. */
  Elf64_Shdr header;
  assert(lseek(file, eh.e_shoff + eh.e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
  assert(read(file, &header, sizeof header) >= 0);
  shstrtbl = (char*)load_elf_section(file, header.sh_offset, header.sh_size);
  assert(shstrtbl);
  /*
   * Loop through section headers:
   *  1.  load string table and symbol table
   *  2.  zero out BSS and SBSS segments
   *  3.  find lower and upper bounds of executable instructions
   */
  //  uintptr_t low_bound  = 0-1;
  //  uintptr_t high_bound = 0;
  low_bound  = 0-1;
  high_bound = 0;
  for (int i=0; i<eh.e_shnum; i++) {
    assert(lseek(file, eh.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
    assert(read(file, &header, sizeof header) >= 0);
    if (strcmp(shstrtbl+header.sh_name, ".bss") == 0 ||
	strcmp(shstrtbl+header.sh_name, ".sbss") == 0) {
      memset((void*)header.sh_addr, 0, header.sh_size);
    }
    if (strcmp(shstrtbl+header.sh_name, ".strtab") == 0) {
      strtbl = (char*)load_elf_section(file, header.sh_offset, header.sh_size);
      dieif(strtbl==0, "could not load string table");
    }
    if (strcmp(shstrtbl+header.sh_name, ".symtab") == 0) {
      symtbl = (Elf64_Sym*)load_elf_section(file, header.sh_offset, header.sh_size);
      dieif(symtbl==0, "could not read symbol table");
      num_syms = header.sh_size / sizeof(Elf64_Sym);
    }
    /* find bounds of instruction segment */
    if (header.sh_flags & SHF_EXECINSTR) {
      if (header.sh_addr < low_bound)
	low_bound = header.sh_addr;
      if (header.sh_addr+header.sh_size > high_bound)
	high_bound = header.sh_addr+header.sh_size;
    }
  }
  //  insnSpace.base = low_bound;
  //  insnSpace.bound = high_bound;
  //  fprintf(stderr, "Text segment [0x%lx, 0x%lx)\n", low_bound, high_bound);
  //insnSpace_init(low_bound, high_bound);
  close(file);
  
  //  info->stack_top = MEM_END + 0x1000;
  info->stack_top = MEM_END;
  stack_lowest = (long)mmap((void*)(info->stack_top-STACK_SIZE), STACK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  dieif(stack_lowest != info->stack_top-STACK_SIZE, "Could not allocate stack\n");

  return current.entry;
}


long initialize_stack(int argc, const char** argv, const char** envp)
{
//  fprintf(stderr, "current.stack_top=%lx, phdr_size=%lx\n", current.stack_top, current.phdr_size);

  // copy phdrs to user stack
  size_t stack_top = current.stack_top - current.phdr_size;
  memcpy((void*)stack_top, (void*)current.phdr, current.phdr_size);
  current.phdr = stack_top;

  // copy argv to user stack
  for (size_t i=0; i<argc; i++) {
    size_t len = strlen((char*)(uintptr_t)argv[i])+1;
    stack_top -= len;
    memcpy((void*)stack_top, (void*)(uintptr_t)argv[i], len);
    argv[i] = (char*)stack_top;
  }

  // copy envp to user stack
#if 0
  size_t envc = sizeof(envp) / sizeof(envp[0]);
  for (size_t i = 0; i < envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    stack_top -= len;
    memcpy((void*)stack_top, envp[i], len);
    envp[i] = (char*)stack_top;
  }
#endif
  size_t envc = 0;
  size_t envlen = 0;
  while (envp[envc])
    envlen += strlen(envp[envc++]) + 1;
  for (size_t i=0; i<envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    stack_top -= len;
    memcpy((void*)stack_top, envp[i], len);
    envp[i] = (char*)stack_top;
  }

  // align stack
  stack_top &= -sizeof(void*);

//  fprintf(stderr, "AT_RANDOM = stack_top = 0x%016lx\n", stack_top);

  struct {
    long key;
    size_t value;
  } aux[] = {
    {AT_ENTRY, current.entry},
    {AT_PHNUM, (size_t)current.phnum},
    {AT_PHENT, (size_t)current.phent},
    {AT_PHDR, current.phdr},
    {AT_PAGESZ, RISCV_PGSIZE},
    {AT_SECURE, 0},
    {AT_RANDOM, stack_top},
    {AT_NULL, 0}
  };

  // place argc, argv, envp, auxp on stack
  #define PUSH_ARG(type, value) do { \
    *((type*)sp) = (type)value; \
    sp += sizeof(type); \
  } while (0)

  unsigned naux = sizeof(aux)/sizeof(aux[0]);
  stack_top -= (1 + argc + 1 + envc + 1 + 2*naux) * sizeof(uintptr_t);
  stack_top &= -16;
  long sp = stack_top;
  PUSH_ARG(uintptr_t, argc);
  for (unsigned i = 0; i < argc; i++)
    PUSH_ARG(uintptr_t, argv[i]);
  PUSH_ARG(uintptr_t, 0); /* argv[argc] = NULL */
  for (unsigned i = 0; i < envc; i++)
    PUSH_ARG(uintptr_t, envp[i]);
  PUSH_ARG(uintptr_t, 0); /* envp[envc] = NULL */
  for (unsigned i = 0; i < naux; i++) {
    PUSH_ARG(uintptr_t, aux[i].key);
    PUSH_ARG(uintptr_t, aux[i].value);
  }

  current.stack_top = stack_top;
  return stack_top;
}


int elf_find_symbol(const char* name, long* begin, long* end)
{
  if (strtbl) {
    for (int i=0; i<num_syms; i++) {
      int n = strlen(name);
      if (strncmp(strtbl+symtbl[i].st_name, name, n) == 0) {
	*begin = symtbl[i].st_value;
	if (end)
	  *end = *begin + symtbl[i].st_size;
	return 1;
      }
    }
  }
  return 0;
}


const char* elf_find_pc(long pc, long* offset)
{
  if (symtbl) {
    for (int i=0; i<num_syms; i++) {
      if (symtbl[i].st_value <= pc && pc < symtbl[i].st_value+symtbl[i].st_size) {
	*offset = pc - symtbl[i].st_value;
	return strtbl + symtbl[i].st_name;
      }
    }
  }
  return 0;
}


const char* reg_name[256] = {
  "zero","ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
  "s0",  "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
  "a6",  "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
  "s8",  "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6",
  "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
  "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
  "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
  "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
  "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
  "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
  "vm",
  [0xFF]="NOREG"
};
