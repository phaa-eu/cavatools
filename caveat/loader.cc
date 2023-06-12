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

#include <string>
#include <map>

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

long load_elf_binary(const char* file_name, int include_data);
int elf_find_symbol(const char* name, long* begin, long* end);
const char* elf_find_pc(long pc, long* offset);

long initialize_stack(int argc, const char** argv, const char** envp);
long emulate_brk(long addr);
  

//extern std::map<long, const char*> fname; // dictionary of pc->name
extern std::map<long, std::string> fname; // dictionary of pc->name

/*
  Utility stuff.
*/
#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }
#define dbmsg(fmt, ...)		       { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n"); }

#define MEM_END		0x60000000L
#define STACK_SIZE	0x01000000L
#define BRK_SIZE	0x01000000L

struct pinfo_t current;

static long phdrs[128];

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


void read_elf_symbols(const char* filename, uintptr_t bias)
{
  dbmsg("reading %s symbols", filename);
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
    if (strcmp(shstrtbl+shdr.sh_name, ".symtab") == 0) {
      long num_syms = shdr.sh_size / sizeof(Elf64_Sym);
      dbmsg("num_syms=%ld", num_syms);
      //      Elf64_Sym symtbl[num_syms];
      dieif(lseek(fd, shdr.sh_offset, SEEK_SET)<0, "lseek symtbl failed");
      Elf64_Sym sb;
      for (int k=0; k<shdr.sh_size/sizeof(Elf64_Sym); k++) {
	dieif(read(fd, &sb, sizeof(Elf64_Sym))!=sizeof(Elf64_Sym), "read symtbl failed");
	if (ELF64_ST_TYPE(sb.st_info) == STT_FUNC)
	  fname[sb.st_value + bias] = strtbl + sb.st_name;
      }
    }
  }

  //  for (auto it=fname.begin(); it!=fname.end(); it++)
  //    fprintf(stderr, "%16lx  %s\n", it->first, it->second);
  delete strtbl;
  close(fd);
}

long load_elf_file(const char* file_name, uintptr_t bias, pinfo_t* info)
{
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

  info->phdr = (uint64_t)phdrs;
  info->phdr_size = sizeof(phdrs);
  info->phnum = eh.e_phnum;
  info->phent = sizeof(Elf64_Phdr);
  info->entry = eh.e_entry + bias;
  
  phdr_size = eh.e_phnum * sizeof(Elf64_Phdr);
  quitif(phdr_size > info->phdr_size, "Phdr too big");
  
  info->phdr_size = sizeof(phdrs);

  dieif(lseek(file, eh.e_phoff, SEEK_SET) < 0, "lseek failed");
  dieif(read(file, (void*)info->phdr, phdr_size) != (ssize_t)phdr_size, "read(phdr) failed");
  Elf64_Phdr* ph = (Elf64_Phdr*)info->phdr;

  // don't load dynamic linker at 0, else we can't catch NULL pointer derefs
  //  uintptr_t bias = 0;
  //  if (eh.e_type == ET_DYN)
  //    bias = RISCV_PGSIZE;
  
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
      dbmsg("[%8lx, %8lx, %8lx) mapping from file", vaddr-prepad, vaddr, (vaddr-prepad)+(ph[i].p_filesz+prepad));
      
      //      memset((void*)(vaddr-prepad), 0, prepad);
      
      //      if (!(prot & PROT_WRITE))
      //        dieif(mprotect((void*)(vaddr-prepad), ph[i].p_filesz + prepad, prot), "Could not mprotect()\n");
      size_t mapped = ROUNDUP(ph[i].p_filesz + prepad, RISCV_PGSIZE) - prepad;
      if (ph[i].p_memsz > mapped) {
        dieif(mmap((void*)(vaddr+mapped), ph[i].p_memsz - mapped, prot, flags|MAP_ANONYMOUS, 0, 0) != (void*)(vaddr+mapped), "Could not mmap()\n");
	
	dbmsg("[%8lx, %8s, %8lx) zero mapped", vaddr+mapped, "", (vaddr+mapped)+ph[i].p_memsz);
      }
    }
    info->brk_max = info->brk_min + BRK_SIZE;
  }

  /* Read section header string table. */
  Elf64_Shdr header;
  assert(lseek(file, eh.e_shoff + eh.e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
  assert(read(file, &header, sizeof header) >= 0);

  dieif(lseek(file, header.sh_offset, SEEK_SET) < 0, "lseek failed");
  shstrtbl = new char[header.sh_size];
  dieif(read(file, shstrtbl, header.sh_size) != (ssize_t)header.sh_size, "read shstrtbl failed");
  
  //  shstrtbl = (char*)load_elf_section(file, header.sh_offset, header.sh_size);
  //  assert(shstrtbl);

  for (int i=eh.e_shnum-1; i>=0; i--) {
    assert(lseek(file, eh.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET) >= 0);
    assert(read(file, &header, sizeof header) >= 0);
    //    dbmsg("section[%2d] alloc=%d nobits=%d %s", i, (header.sh_flags&SHF_ALLOC)!=0, (header.sh_type&SHT_NOBITS)!=0, shstrtbl+header.sh_name);
    if (strcmp(shstrtbl+header.sh_name, ".bss") == 0 ||
	//	strcmp(shstrtbl+header.sh_name, ".tbss") == 0 ||
	strcmp(shstrtbl+header.sh_name, ".sbss") == 0) {
      memset((void*)(header.sh_addr+bias), 0, header.sh_size);
    }
  }
  close(file);
  delete shstrtbl;
  return info->entry;
}

long load_elf_binary( const char* file_name, int include_data )
/* file_name	- name of ELF binary, must be statically linked for now
   include_data	- 1=load DATA and BSS segments, 0=load TEXT only
   returns entry point address */
{
  long entry = load_elf_file(file_name, 0, &current);
  read_elf_symbols(file_name, 0);
  return entry;
}


struct aux_t {
  long key;
  size_t value;
};

long initialize_stack(int argc, const char** argv, const char** envp)
{
  // allocate stack space
  void* stack_lowest = mmap((void*)(MEM_END-STACK_SIZE), STACK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  dieif(stack_lowest != (void*)(MEM_END-STACK_SIZE), "Could not allocate stack\n");
  uintptr_t stack_top = MEM_END;

  // first comes copy of program header
  stack_top -= current.phdr_size;
  memcpy((void*)stack_top, (void*)current.phdr, current.phdr_size);

#define PUSH_STR(x) do {					\
    stack_top -= strlen(x) + 1;					\
    memcpy((void*)stack_top, (x), strlen(x)+1);			\
  } while (0)
    
  // envp strings
  int envc = 0;
  while (envp[envc]) {
    PUSH_STR(envp[envc]);
    envp[envc++] = (const char*)stack_top;
  }
  
  // argv strings
  for (int i=0; i<argc; i++)
    PUSH_STR(argv[i]);

  // align stack
  stack_top &= -16;
  current.stack_top = stack_top;

#define PUSH_ARG(value) do {				\
    stack_top -= sizeof(uintptr_t);			\
    *((uintptr_t*)stack_top) = (uintptr_t)value;	\
  } while (0)

  // auxv terminated with this entry
  PUSH_ARG(0);
  PUSH_ARG(AT_NULL);
  // auxv is after envp
  for (struct aux_t* auxv=(struct aux_t*)(&envp[envc+1]); auxv->key != AT_NULL; auxv++) {
    size_t value = auxv->value;
    switch (auxv->key) {
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
    default:
      continue;
    }
    PUSH_ARG(value);
    PUSH_ARG(auxv->key);
  }

  // envp[]
  PUSH_ARG(0);
  for (int i=envc-1; i>=0; i--)
    PUSH_ARG(envp[i]);

  // argvp[]
  PUSH_ARG(0);
  for (int i=argc-1; i>=0; i--)
    PUSH_ARG(argv[i]);

  // argc
  PUSH_ARG(argc);

  current.stack_top = stack_top;
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
