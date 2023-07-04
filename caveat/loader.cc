/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <elf.h>

#include <string>
#include <map>

#define RISCV_PGSHIFT 12
#define RISCV_PGSIZE (1 << RISCV_PGSHIFT)

#define ROUNDUP(a, b) ((((a)-1)/(b)+1)*(b))
#define ROUNDDOWN(a, b) ((a)/(b)*(b))

/*  Process information  */
struct pinfo_t {
  uintptr_t phnum;
  uintptr_t entry;
  char* path;
  uintptr_t brk;
  uintptr_t brk_min;
  uintptr_t brk_max;
};

struct pinfo_t current;
struct pinfo_t interp;
uintptr_t phdrs = 0xdeadbeef;

//long load_elf_binary(const char* file_name, int include_data);
//int elf_find_symbol(const char* name, long* begin, long* end);
//const char* elf_find_pc(long pc, long* offset);

//long initialize_stack(int argc, const char** argv, const char** envp, struct pinfo_t*);

long emulate_brk(long addr);
  
extern std::map<long, std::string> fname; // dictionary of pc->name

std::map<std::string, long> symaddr;

/*
  Utility stuff.
*/
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }
//#define dbmsg(fmt, ...)		       { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n"); }
#define dbmsg(fmt, ...)

#define MEM_END		0x60000000L
#define STACK_SIZE	0x01000000L
#define BRK_SIZE	0x01000000L

#define INTERP_BASE	MEM_END

static char* strtbl;
static Elf64_Sym* symtbl;
static long num_syms;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(a, lo, hi) MIN(MAX(a, lo), hi)

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


static void read_elf_symbols(const char* filename, uintptr_t bias)
{
  dbmsg("reading %s symbols, bias=0x%lx", filename, bias);
  int fd = open(filename, O_RDONLY, 0);
  quitif(fd<0, "Unable to open ELF binary file \"%s\"\n", filename);
  Elf64_Ehdr eh;
  dieif(read(fd, &eh, sizeof(eh))!=sizeof(eh), "read elf header failed");
  quitif(!(eh.e_ident[0] == '\177' && eh.e_ident[1] == 'E' &&
	   eh.e_ident[2] == 'L'    && eh.e_ident[3] == 'F'),
	 "Elf header not correct");
  
  /* Read section header string table. */
  dieif(lseek(fd, eh.e_shoff + eh.e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET)<0, "seek string section header failed");
  Elf64_Shdr shdr;
  dieif(read(fd, &shdr, sizeof shdr)<0, "read string section header failed");
  char shstrtbl[shdr.sh_size];
  dieif(lseek(fd, shdr.sh_offset, SEEK_SET)<0, "lseek shstrtbl failed");
  dieif(read(fd, shstrtbl, shdr.sh_size)!=shdr.sh_size, "read shstrtbl failed");

  // First find string table and copy into memory
  char* strtbl = 0;
  for (int i=eh.e_shnum-1; i>=0; i--) {
    dieif(lseek(fd, eh.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET)<0, "seek section header failed");
    dieif(read(fd, &shdr, sizeof shdr)<0, "read section header failed");
    if (strcmp(shstrtbl+shdr.sh_name, ".strtab") == 0) {
      dbmsg("%s size %ld\n", shstrtbl+shdr.sh_name, shdr.sh_size);
      
    	//    if (strcmp(shstrtbl+shdr.sh_name, ".strtab") == 0 ||
	//	strcmp(shstrtbl+shdr.sh_name, ".dynstr") == 0) {
      strtbl = new char[shdr.sh_size];
      dieif(lseek(fd, shdr.sh_offset, SEEK_SET)<0, "lseek strtbl failed");
      dieif(read(fd, strtbl, shdr.sh_size)!=shdr.sh_size, "read strtbl failed");
      break;
    }
  }
  // Next read symbol tables
  for (int i=eh.e_shnum-1; i>=0; i--) {
    dieif(lseek(fd, eh.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET)<0, "seek section header failed");
    dieif(read(fd, &shdr, sizeof shdr)<0, "read section header failed");
    
    //    if (strcmp(shstrtbl+shdr.sh_name, ".symtab") == 0  ||
    //	strcmp(shstrtbl+shdr.sh_name, ".dynsym") == 0) {
    
    if (strcmp(shstrtbl+shdr.sh_name, ".symtab") == 0) {
      long num_syms = shdr.sh_size / sizeof(Elf64_Sym);
      dbmsg("%s num_syms=%ld", shstrtbl+shdr.sh_name, num_syms);
      //      Elf64_Sym symtbl[num_syms];
      dieif(lseek(fd, shdr.sh_offset, SEEK_SET)<0, "lseek symtbl failed");
      Elf64_Sym sb;
      for (int k=0; k<shdr.sh_size/sizeof(Elf64_Sym); k++) {
	dieif(read(fd, &sb, sizeof(Elf64_Sym))!=sizeof(Elf64_Sym), "read symtbl failed");
	//dbmsg("%16lx %s", sb.st_value+bias, strtbl+sb.st_name);
	
	//	if (ELF64_ST_TYPE(sb.st_info) == STT_FUNC)
	  fname[sb.st_value + bias] = strtbl + sb.st_name;
	if (ELF64_ST_TYPE(sb.st_info) == STT_OBJECT)
	  symaddr[strtbl + sb.st_name] = sb.st_value + bias;
	  //	  objsym][strtbl + sb.st_name = { .addr=sb.st_value+bias, .size=sb.st_size };
      }
      break;
    }
  }
  
  // hack to inhibit ld.so.cache
  if (symaddr.count("_rtld_global_ro")) {
    dbmsg("_rtld_global_ro 0x%lx setting to 1", symaddr["_rtld_global_ro"]);
    int* p = (int*)(symaddr["_rtld_global_ro"] + 32);
    *p = 1;
  }
  else
    dbmsg("_rtld_global_ro not found");
	  
  //  for (auto it=fname.begin(); it!=fname.end(); it++)
  //    fprintf(stderr, "%16lx  %s\n", it->first, const_cast<const char*>(it->second.c_str()));
  
  //  delete strtbl;
  close(fd);
}

static uintptr_t at_base = 0;
static uintptr_t hack_bias;

static long load_elf_file(const char* file_name, uintptr_t bias, pinfo_t* info)
{
  dbmsg("Loading %s, bias=%lx", file_name, bias);
  int flags = MAP_FIXED | MAP_PRIVATE;
  ssize_t ehdr_size;
  size_t phdr_size;
  long number_of_insn;
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

  if (eh.e_type == ET_DYN && bias == 0) {
    bias = INTERP_BASE;
    hack_bias = bias;
  }
  
  info->phnum = eh.e_phnum;
  info->entry = eh.e_entry + bias;
  uintptr_t entry = info->entry; // may be overwritten if PT_INTERP
  info->path = new char[strlen(file_name)+1];
  strcpy(info->path, file_name);

  dieif(lseek(file, eh.e_phoff, SEEK_SET) < 0, "lseek failed");
  Elf64_Phdr ph[eh.e_phnum];
  dieif(read(file, (void*)ph, phdr_size) != (ssize_t)phdr_size, "read(phdr) failed");

  // load dynamic loader if necessary (recursion)
  for (int i = eh.e_phnum - 1; i >= 0; i--) {
    if (ph[i].p_type == PT_INTERP) {
      const char* sysroot = "/opt/riscv/sysroot";
      char lib_name[strlen(sysroot) + ph[i].p_filesz + 1];
      strcpy(lib_name, sysroot);
      dieif(lseek(file, ph[i].p_offset, SEEK_SET) < 0, "PT_INTERP lseek failed");
      dieif(read(file, lib_name+strlen(sysroot), ph[i].p_filesz) != ph[i].p_filesz, "PT_INTERP read failed");
      fprintf(stderr, "loading dynamic %s\n", lib_name);
      entry = load_elf_file(lib_name, INTERP_BASE, &interp);
      read_elf_symbols(lib_name, INTERP_BASE);
      break;
    }
  }
  
  // find address range of loaded sections
  uintptr_t first = -1L;
  uintptr_t last = 0L;
  for (int i=eh.e_phnum-1; i>=0; i--) {
    if (ph[i].p_type != PT_LOAD)
      continue;
    uintptr_t begin = ph[i].p_vaddr + bias;
    uintptr_t end = begin + ph[i].p_memsz;
    if (begin < first)
      first = begin;
    if (end > last)
      last = end;
  }
  // set brk to end of initialized area
  info->brk_min = last;
  info->brk_max = last + BRK_SIZE;
  // round down to page boundry to get mmap region
  uintptr_t prepad = first % RISCV_PGSIZE;
  void* base = (void*)(first - prepad);
  size_t len = last - first + prepad;
  dieif(mmap(base, len, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0, 0)!=base, "mmap() failed");
  // first read file header into prepad
  dieif(lseek(file, 0, SEEK_SET) < 0, "lseek failed");
  dieif(read(file, base, prepad)!=prepad, "read failed");
  for (int i=0; i<eh.e_phnum; i++) {
    if (ph[i].p_type != PT_LOAD)
      continue;
    dieif(lseek(file, ph[i].p_offset, SEEK_SET) < 0, "lseek failed");
    dieif(read(file, (void*)(ph[i].p_vaddr+bias), ph[i].p_filesz)!=ph[i].p_filesz, "read failed");
    // check for .bss and friends
    size_t zeros = ph[i].p_memsz - ph[i].p_filesz;
    if (zeros > 0) {
      uintptr_t begin = ph[i].p_vaddr + bias + ph[i].p_filesz;
      memset((void*)begin, 0, zeros);
    }
  }
  // record program header address for aux vector
  phdrs = (uintptr_t)base + eh.e_phoff;

  /* Read section header string table. */
  Elf64_Shdr header;
  assert(lseek(file, eh.e_shoff + eh.e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
  assert(read(file, &header, sizeof header) >= 0);

  dieif(lseek(file, header.sh_offset, SEEK_SET) < 0, "lseek failed");
  shstrtbl = new char[header.sh_size];
  dieif(read(file, shstrtbl, header.sh_size) != (ssize_t)header.sh_size, "read shstrtbl failed");

  for (int i=eh.e_shnum-1; i>=0; i--) {
    assert(lseek(file, eh.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
    assert(read(file, &header, sizeof header) >= 0);
    if (strcmp(shstrtbl+header.sh_name, ".bss") == 0 ||
	//	strcmp(shstrtbl+header.sh_name, ".tbss") == 0 ||
	strcmp(shstrtbl+header.sh_name, ".sbss") == 0) {
      memset((void*)(header.sh_addr+bias), 0, header.sh_size);
    }
  }

  close(file);
  delete shstrtbl;
  return entry;
}

static long load_elf_binary(const char* file_name)
{
  long entry = load_elf_file(file_name, 0, &current);
  read_elf_symbols(file_name, hack_bias);
  //read_elf_symbols(file_name, 0);
  return entry;
}


struct aux_t {
  long key;
  size_t value;
};

static long initialize_stack(int argc, const char** argv, const char** envp, pinfo_t* info)
{
  // allocate stack space
  void* stack_lowest = mmap((void*)(MEM_END-STACK_SIZE), STACK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  dieif(stack_lowest != (void*)(MEM_END-STACK_SIZE), "Could not allocate stack\n");
  uintptr_t stack_top = MEM_END;

  // first comes copy of program header
  int fd = open(argv[0], O_RDONLY, 0);
  quitif(fd<0, "Unable to open ELF binary file \"%s\"\n", argv[0]);
  Elf64_Ehdr eh;
  dieif(read(fd, &eh, sizeof(eh))!=sizeof(eh), "read elf header failed");
  {
    //    size_t phdr_size = eh.e_phnum * sizeof(Elf64_Phdr);
    size_t phdr_size = eh.e_phoff + eh.e_phnum * sizeof(Elf64_Phdr);
    //    size_t phdr_size = 4096;
    stack_top -= phdr_size;
    //    dieif(lseek(fd, eh.e_phoff, SEEK_SET) < 0, "lseek failed");
    dieif(lseek(fd, 0, SEEK_SET) < 0, "lseek failed");
    dieif(read(fd, (void*)stack_top, phdr_size)!=phdr_size, "read(phdr) failed");
  }
  close(fd);
  //  uintptr_t phdrs = stack_top;
  //  uintptr_t phdrs = stack_top + eh.e_phoff;

  // copy string onto stack
#define PUSH_STR(x)  ( stack_top-=strlen(x)+1, memcpy((void*)stack_top, (x), strlen(x)+1), (const char*)x )
  
  // argv strings
  //  for (int i=0; i<argc; i++)
  for (int i=0; argv[i]; i++)
    argv[i] = PUSH_STR(argv[i]);

  // envp strings
  int envc = 0;
  while (envp[envc]) {
    PUSH_STR(envp[envc]);
    envc++;
  }

  // align stack
  stack_top &= -16;
  uintptr_t at_random = stack_top;

#define PUSH_ARG(value) do {				\
    stack_top -= sizeof(uintptr_t);			\
    *((uintptr_t*)stack_top) = (uintptr_t)value;	\
  } while (0)

  // auxv terminated with NULL entry
  PUSH_ARG(0);
  PUSH_ARG(AT_NULL);

  for (struct aux_t* auxv=(struct aux_t*)(&envp[envc+1]); auxv->key != AT_NULL; auxv++) {
    size_t value = auxv->value;
    switch (auxv->key) {
      
    case AT_PAGESZ:	value = RISCV_PGSIZE; break;
    case AT_PHDR:	value = phdrs; break;
    case AT_PHENT:	value = sizeof(Elf64_Phdr); break;
    case AT_PHNUM:	value = info->phnum; break;
    case AT_ENTRY:	value = info->entry; break;
      
    case AT_SECURE:	value = 0; break;
    case AT_RANDOM:	value = at_random; break;
    case AT_EXECFN:	value = (size_t)argv[0]; break;
    case AT_PLATFORM:	value = (size_t)"riscv64"; break;
      
    case AT_HWCAP:	  continue;
    case AT_HWCAP2:	  continue;
    case AT_SYSINFO_EHDR: continue; /* No vDSO */
      
    case AT_BASE:
      if (at_base == 0)  continue;
      value = at_base;
      break;

    case AT_UID:
    case AT_EUID:
    case AT_GID:
    case AT_EGID:
      break;
      
    default:
      continue;
    }
    PUSH_ARG(value);
    PUSH_ARG(auxv->key);
  }
  
  // envp[]
  PUSH_ARG(0);

  for (int i=envc-1; i>=0; i--) {
    if (envp[i] == 0)
      continue;
    PUSH_ARG(envp[i]);
  }

  // argvp[]
  PUSH_ARG(0);
  for (int i=argc-1; i>=0; i--)
    PUSH_ARG(argv[i]);

  // argc
  PUSH_ARG(argc);
  
  return stack_top;
}

long emulate_execve(const char* filename, int argc, const char* argv[], const char* envp[], uintptr_t& pc)
{
  pc = load_elf_binary(argv[0]);
  dbmsg("interp.base=%lx", at_base);
  return initialize_stack(argc, argv, envp, &current);
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
