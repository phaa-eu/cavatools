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

const char* sysroot = "/opt/riscv/sysroot";
extern long gdb_text, gdb_data, gdb_bss;

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

#define BIAS  MEM_END
//#define BIAS  0x10000L

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
  //  void* where = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  void* where = new char[size];
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
    bias = BIAS;
  info->entry = eh.e_entry + bias;

  for (int i=0; i<eh.e_phnum; i++) {
    if (ph[i].p_type == PT_INTERP) {
      long interp_bias = BIAS;
      char lib_name[strlen(sysroot) + ph[i].p_filesz + 1];
      strcpy(lib_name, sysroot);
      dieif(lseek(file, ph[i].p_offset, SEEK_SET) < 0, "PT_INTERP lseek failed");
      dieif(read(file, lib_name+strlen(sysroot), ph[i].p_filesz) != ph[i].p_filesz, "PT_INTERP read failed");
      fprintf(stderr, "opening %s\n", lib_name);
      int libfd = open(lib_name, O_RDONLY, 0);
      dieif(libfd<0, "Unable to open dynamic linker %s", lib_name);
      
      struct stat s;
      dieif(fstat(libfd, &s)<0, "fstat libfd failed");
      Elf64_Ehdr* lib_eh = (Elf64_Ehdr*)mmap((void*)interp_bias, s.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, libfd, 0);
      dieif(lib_eh != (void*)interp_bias, "mmap dynamic linker failed");
      //      current.entry = lib_eh->e_entry + interp_bias;
      info->entry = 0x104a8L + interp_bias;
      //      info->entry = 0xfe02L + interp_bias;
    
      // load symbol table
      Elf64_Shdr header;
      assert(lseek(libfd, lib_eh->e_shoff + lib_eh->e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
      assert(read(libfd, &header, sizeof header) >= 0);
      char* shstrtbl = (char*)load_elf_section(libfd, header.sh_offset, header.sh_size);
      assert(shstrtbl);
      char* strtbl;
    
      for (int i=lib_eh->e_shnum-1; i>=0; i--) {
	assert(lseek(libfd, lib_eh->e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
	assert(read(libfd, &header, sizeof header) >= 0);
	if (strcmp(shstrtbl+header.sh_name, ".strtab") == 0) {
	  strtbl = new char[header.sh_size];
	  dieif(lseek(libfd, header.sh_offset, SEEK_SET)<0, "lseek strtbl failed");
	  dieif(read(libfd, strtbl, header.sh_size)!=header.sh_size, "read strtbl failed");
	}
	else if (strcmp(shstrtbl+header.sh_name, ".symtab") == 0) {
	  Elf64_Sym* symtbl = (Elf64_Sym*)load_elf_section(libfd, header.sh_offset, header.sh_size);
	  dieif(symtbl==0, "could not read symbol table");
	  int num_syms = header.sh_size / sizeof(Elf64_Sym);

	  for (int k=0; k<num_syms; k++) {
	    Elf64_Sym* s = &symtbl[k];
	    if (ELF64_ST_TYPE(s->st_info) == STT_FUNC)
	      fname[s->st_value + interp_bias] = &strtbl[s->st_name];
	  }
	}
	else if (strcmp(shstrtbl+header.sh_name, ".bss") == 0 ||
	    strcmp(shstrtbl+header.sh_name, ".sbss") == 0) {
	  memset((void*)(header.sh_addr+interp_bias), 0, header.sh_size);
	}
#if 1
	if (strcmp(shstrtbl+header.sh_name, ".text") == 0)
	  gdb_text = header.sh_addr + interp_bias;
	else if (strcmp(shstrtbl+header.sh_name, ".rodata") == 0)
	  gdb_data = header.sh_addr + interp_bias;
	else if (strcmp(shstrtbl+header.sh_name, ".bss") == 0)
	  gdb_bss = header.sh_addr + interp_bias;
#endif
      }
      goto cleanup;
    }
  } // for

  // static program;
  for (int i=eh.e_phnum-1; i>=0; i--) {
    //fprintf(stderr, "section %d p_vaddr=0x%lx p_memsz=0x%lx\n", i, ph[i].p_vaddr, ph[i].p_memsz);
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
  for (int i=eh.e_shnum-1; i>=0; i--) {
    assert(lseek(file, eh.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
    assert(read(file, &header, sizeof header) >= 0);
    if (strcmp(shstrtbl+header.sh_name, ".bss") == 0 ||
	strcmp(shstrtbl+header.sh_name, ".sbss") == 0) {
      memset((void*)(header.sh_addr+bias), 0, header.sh_size);
    }
    if (strcmp(shstrtbl+header.sh_name, ".strtab") == 0) {
      //      strtbl = (char*)load_elf_section(file, header.sh_offset, header.sh_size);
      //      dieif(strtbl==0, "could not load string table");
      //      if (strcmp(shstrtbl+header.sh_name, ".strtab") == 0) {
      strtbl = new char[header.sh_size];
      dieif(lseek(file, header.sh_offset, SEEK_SET)<0, "lseek strtbl failed");
      dieif(read(file, strtbl, header.sh_size)!=header.sh_size, "read strtbl failed");
    }
    if (strcmp(shstrtbl+header.sh_name, ".symtab") == 0) {
      symtbl = (Elf64_Sym*)load_elf_section(file, header.sh_offset, header.sh_size);
      dieif(symtbl==0, "could not read symbol table");
      num_syms = header.sh_size / sizeof(Elf64_Sym);

      for (int k=0; k<num_syms; k++) {
	Elf64_Sym* s = &symtbl[k];
	if (ELF64_ST_TYPE(s->st_info) == STT_FUNC)
	  fname[s->st_value + bias] = strtbl + s->st_name;
      }
    }
  }

 cleanup:
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
  size_t sp = current.stack_top - current.phdr_size;
  memcpy((void*)sp, (void*)current.phdr, current.phdr_size);
  current.phdr = sp;

  // copy argv strings to user stack
  for (size_t i=0; i<argc; i++) {
    size_t len = strlen((char*)(uintptr_t)argv[i])+1;
    sp -= len;
    memcpy((void*)sp, (void*)(uintptr_t)argv[i], len);
    argv[i] = (char*)sp;
  }
  // copy envp strings to user stack
  size_t envc = 0;
  while (envp[envc]) {
    size_t len = strlen(envp[envc]) + 1;
    sp -= len;
    memcpy((void*)sp, envp[envc], len);
    envp[envc++] = (char*)sp;
  }
  // align stack
  sp &= -sizeof(void*);
  current.stack_top = sp;

  // helper macro
#define PUSH_ARG(type, value) do {		\
    sp -= sizeof(type);				\
    *((type*)sp) = (type)value;			\
  } while (0)
  
  // copy auxv to stack
  struct aux_t {
    long key;
    size_t value;
    aux_t(long k, size_t v) { key=k; value=v; }
  };
  struct aux_t* auxv = (struct aux_t*)(envp+envc+1);

  PUSH_ARG(aux_t, aux_t(AT_NULL,0));
  for (int i=0; ; i++) {
    size_t value = auxv[i].value;
    size_t old = value;
    switch (auxv[i].key) {
    case AT_SYSINFO_EHDR:  continue; /* No vDSO */
    case AT_PAGESZ:	value = RISCV_PGSIZE; break;
    case AT_PHDR:	value = current.phdr; break;
    case AT_PHENT:	value = (size_t)current.phent; break;
    case AT_PHNUM:	value = (size_t)current.phnum; break;
    case AT_ENTRY:	value = current.entry; break;
    case AT_SECURE:	value = 0; break;
    case AT_RANDOM:	value = current.stack_top; break;
      //    case AT_EXECFN:	fprintf(stderr, "AT_EXECFN=%s, become %s\n", (char*)value, argv[0]); value = (size_t)argv[0]; break;
    case AT_EXECFN:	value = (size_t)argv[0]; break;
    case AT_PLATFORM:	value = (size_t)"riscv64"; break;
    case AT_BASE:	value = BIAS; break;
      
    case AT_HWCAP:
    case AT_HWCAP2:
      value = 0;
      break;

    case AT_UID:
    case AT_EUID:
    case AT_GID:
    case AT_EGID:
      break;
      
    case AT_NULL:
      goto end_of_auxv;
      
    default:
      continue;
    }
    PUSH_ARG(aux_t, aux_t(auxv[i].key,value));
    //    fprintf(stderr, "auxv[%d]= (%ld, value=%lx), was %lx\n", i, auxv[i].key, value, old);
  }
 end_of_auxv:
  // copy envp[] to stack
  PUSH_ARG(uintptr_t, 0); // last envp = NULL
  for (int i=envc-1; i>=0; i--) {
    PUSH_ARG(uintptr_t, envp[i]);
    //    fprintf(stderr, "envp[%d]=%s\n", i, envp[i]);
  }
  // copy argv[] to stack
  PUSH_ARG(uintptr_t, 0); // last argv = NULL
  for (int i=argc-1; i>=0; i--) {
    PUSH_ARG(uintptr_t, argv[i]);
    //    fprintf(stderr, "argv[%d]=%s\n", i, argv[i]);
  }

#if 0
  // add extra argv
  PUSH_ARG(uintptr, (uintptr_t)"--list"); argc++;
  PUSH_ARG(uintptr, (uintptr_t)"--inhibit-cache"); argc++;
  PUSH_ARG(uintptr, (uintptr_t)"/opt/riscv/sysroot/lib"); argc++;
  PUSH_ARG(uintptr, (uintptr_t)"--library-path"); argc++;
  PUSH_ARG(uintptr, (uintptr_t)"/opt/riscv/sysroot/lib/ld-linux-riscv64-lp64d.so.1"); argc++;
#endif

  // put argc on stack
  PUSH_ARG(uintptr_t, argc);
  //  fprintf(stderr, "argc=%d\n", argc);

  current.stack_top = sp;
  return sp;
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
