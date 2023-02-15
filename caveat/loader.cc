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

#include <map>

extern std::map<long, const char*> fname; // dictionary of pc->name

/*
  Utility stuff.
*/
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }

#define RISCV_PGSHIFT 12
#define RISCV_PGSIZE (1 << RISCV_PGSHIFT)

#define MEM_END		0x60000000L
#define STACK_SIZE	0x01000000L
#define BRK_SIZE	0x01000000L

#define ROUNDUP(a, b) ((((a)-1)/(b)+1)*(b))
#define ROUNDDOWN(a, b) ((a)/(b)*(b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(a, lo, hi) MIN(MAX(a, lo), hi)

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

static struct pinfo_t current;
static long phdrs[128];
static char* strtbl;
static Elf64_Sym* symtbl;
static long num_syms;

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
      //      if (!(prot & PROT_WRITE))
      //        dieif(mprotect((void*)(vaddr-prepad), ph[i].p_filesz + prepad, prot), "Could not mprotect()\n");
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
  uintptr_t low_bound  = 0-1;
  uintptr_t high_bound = 0;
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

      for (int k=0; k<num_syms; k++) {
	Elf64_Sym* s = &symtbl[k];
	if (ELF64_ST_TYPE(s->st_info) == STT_FUNC)
	  fname[s->st_value] = strtbl + s->st_name;
      }
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


long emulate_brk(long addr)
{
  struct pinfo_t* info = &current;
  long newbrk = addr;
  if (addr < info->brk_min)
    newbrk = info->brk_min;
  else if (addr > info->brk_max)
    newbrk = info->brk_max;

  if (info->brk == 0)
    info->brk = ROUNDUP(info->brk_min, RISCV_PGSIZE);

  long newbrk_page = ROUNDUP(newbrk, RISCV_PGSIZE);
  if (info->brk > newbrk_page)
    munmap((void*)newbrk_page, info->brk - newbrk_page);
  else if (info->brk < newbrk_page)
    assert(mmap((void*)info->brk, newbrk_page - info->brk, -1, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0, 0) == (void*)info->brk);
  info->brk = newbrk_page;

  return newbrk;
}



long initialize_stack(int argc, const char** argv, const char** envp)
{
//  fprintf(stderr, "current.stack_top=%lx, phdr_size=%lx\n", current.stack_top, current.phdr_size);

  // copy phdrs to user stack
  size_t stack_top = current.stack_top - current.phdr_size;
  memcpy((void*)stack_top, (void*)current.phdr, current.phdr_size);
  current.phdr = stack_top;

  // copy argv strings to user stack
  for (size_t i=0; i<argc; i++) {
    size_t len = strlen((char*)(uintptr_t)argv[i])+1;
    stack_top -= len;
    memcpy((void*)stack_top, (void*)(uintptr_t)argv[i], len);
    argv[i] = (char*)stack_top;
  }
  // copy envp strings to user stack
  size_t envc = 0;
  size_t envlen = 0;
  while (envp[envc]) {
    envlen += strlen(envp[envc]) + 1;
    envc++;
  }
  for (size_t i=0; i<envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    stack_top -= len;
    memcpy((void*)stack_top, envp[i], len);
    envp[i] = (char*)stack_top;
  }
  // compute size of auxv area
  struct aux_t {
    long key;
    size_t value;
  };
  struct aux_t* auxv = (struct aux_t*)(&envp[envc+1]);
  size_t auxc = 0;
  do {
    auxc++;
  } while (auxv[auxc-1].key != AT_NULL);
  stack_top -= auxc*sizeof(struct aux_t);
  // align stack
  stack_top &= -sizeof(void*);

  // place argc, argv, envp, auxp on stack
  #define PUSH_ARG(type, value) do { \
    *((type*)sp) = (type)value; \
    sp += sizeof(type); \
  } while (0)
  
  stack_top -= (1 + argc + 1 + envc + 1 + 2*auxc) * sizeof(uintptr_t);
  stack_top &= -16;		/* align */
  current.stack_top = stack_top;
  long sp = stack_top;
  PUSH_ARG(uintptr_t, argc);
  for (unsigned i = 0; i < argc; i++)
    PUSH_ARG(uintptr_t, argv[i]);
  PUSH_ARG(uintptr_t, 0); /* argv[argc] = NULL */
  for (unsigned i = 0; i < envc; i++)
    PUSH_ARG(uintptr_t, envp[i]);
  PUSH_ARG(uintptr_t, 0); /* envp[envc] = NULL */
  
  for (unsigned i = 0; i < auxc; i++) {
    size_t value = auxv[i].value;
    size_t old = value;
    switch (auxv[i].key) {
      //case AT_SYSINFO_EHDR:  continue; /* No vDSO */
      //    case AT_HWCAP:	value = 0; break;
    case AT_PAGESZ:	value = RISCV_PGSIZE; break;
    case AT_PHDR:	value = current.phdr; break;
    case AT_PHENT:	value = (size_t)current.phent; break;
    case AT_PHNUM:	value = (size_t)current.phnum; break;
      //    case AT_BASE:	value = 0XdeadbeefcafebabeL; break; /* usually the dynamic linker */
      //case AT_BASE:	continue;
    case AT_ENTRY:	value = current.entry; break;
    case AT_SECURE:	value = 0; break;
    case AT_RANDOM:	value = current.stack_top; break;
      //    case AT_HWCAP2:	value = 0; break;
      //    case AT_EXECFN:	fprintf(stderr, "AT_EXECFN=%s, become %s\n", (char*)value, argv[0]); value = (size_t)argv[0]; break;
      //    case AT_PLATFORM:	value = (size_t)"riscv64"; break;
      //case AT_PLATFORM:	continue;
    case AT_NULL:
    default:
      continue;
      break;
    }
    PUSH_ARG(uintptr_t, auxv[i].key);
    PUSH_ARG(uintptr_t, value);
    //fprintf(stderr, "AT=%ld, value=%lx, was %lx\n", auxv[i].key, value, old);
  } /* last entry was AT_NULL */

  return current.stack_top;
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
