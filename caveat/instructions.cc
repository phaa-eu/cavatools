/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <string>
#include <map>

#include "options.h"
#include "caveat.h"
#include "strand.h"

extern option<long> conf_tcache;

std::map<long, std::string> fname; // dictionary of pc->name

const char* func_name(uintptr_t pc) { return fname.count(pc)==1 ? const_cast<const char*>(fname.at(pc).c_str()) : "NOT FOUND"; }


#define LABEL_WIDTH  16
#define OFFSET_WIDTH  8
int slabelpc(char* buf, uintptr_t pc)
{
  auto it = fname.upper_bound(pc);
  if (it == fname.end()) {
    return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, "NONE", -(OFFSET_WIDTH-1), 0L, pc);
  }
  else {
    it--;
    if (it == fname.end()) {
      return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, "UNKNOWN", -(OFFSET_WIDTH-1), 0L, pc);
    }
    else {
      long offset = pc - it->first;
      const char* name = const_cast<const char*>(it->second.c_str());
      return sprintf(buf, "%*.*s+%*ld %8lx: ", LABEL_WIDTH, LABEL_WIDTH, name, -(OFFSET_WIDTH-1), offset, pc);
    }
  }
}

void labelpc(uintptr_t pc, FILE* f)
{
  char buffer[1024];
  slabelpc(buffer, pc);
  fprintf(f, "%s", buffer);
}

int sdisasm(char* buf, uintptr_t pc, const Insn_t* i)
{
  int n = 0;
  if (i->opcode() == Op_ZERO) {
    n += sprintf(buf, "Nothing here");
    return n;
  }
  uint32_t b = *(uint32_t*)pc;
  if (i->compressed())
    n += sprintf(buf+n, "    %04x  ", b&0xFFFF);
  else
    n += sprintf(buf+n, "%08x  ",     b);
  n += sprintf(buf+n, "%-23s", op_name[i->opcode()]);
  char sep = ' ';
  if (i->rd()  != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rd() ]); sep=','; }
  if (i->rs1() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs1()]); sep=','; }
  if (i->longimmed())    { n += sprintf(buf+n, "%c%ld", sep, i->immed()); }
  else {
    if (i->rs2() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs2()]); sep=','; }
    if (i->rs3() != NOREG) { n += sprintf(buf+n, "%c%s", sep, reg_name[i->rs3()]); sep=','; }
    n += sprintf(buf+n, "%c%ld", sep, i->immed());
  }
  return n;
}

void disasm(uintptr_t pc, const Insn_t* i, const char* end, FILE* f)
{
  char buffer[1024];
  sdisasm(buffer, pc, i);
  fprintf(f, "%s%s", buffer, end);
}




#define CSR_FFLAGS	0x1
#define CSR_FRM		0x2
#define CSR_FCSR	0x3

long get_csr(fcsr_t& fcsr, int what)
{
  switch (what) {
  case CSR_FFLAGS:
    return fcsr.f.flags;
  case CSR_FRM:
    return fcsr.f.rm;
  case CSR_FCSR:
    return fcsr.ui;
  default:
    die("unsupported CSR number %d", what);
  }
}

void set_csr(fcsr_t& fcsr, int what, long val)
{
  switch (what) {
  case CSR_FFLAGS:
    fcsr.f.flags = val;
    break;
  case CSR_FRM:
    fcsr.f.rm = val;
    break;
  case CSR_FCSR:
    fcsr.ui = val;
    break;
  default:
    die("unsupported CSR number %d", what);
  }
}



void Tcache_t::initialize(size_t cachesize, size_t hashtablesize)
{
  _extent = cachesize;
  _hashsize = hashtablesize;
#if 0
  array = new uint64_t[_extent];
  map = new Header_t*[_hashsize];
#else
  array = (uint64_t*)mmap(0, _extent*sizeof(uint64_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  table = (size_t*)mmap(0, _hashsize*sizeof(uint64_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#endif
  list = 0;
  clear();
  
  //running = 0;
  //pause = false;
}

Header_t* Tcache_t::find(uintptr_t pc)
{
#if 0
  Header_t* h = table[hashfunction(pc)];
  while (h) {
    if (h->addr == pc)
      return h;
    //    h = h->next;
  }
#else
  size_t h = table[hashfunction(pc)];
  while (h) {
    Header_t* bb = (Header_t*)&array[h];
    if (bb->addr == pc)
      return bb;
    h = bb->link;
  }
#endif
  return 0;
}

Header_t* Tcache_t::add(Header_t* wbb, size_t n)
{
  // atomically allocate cache space
  size_t begin = __sync_fetch_and_add(&_size, n);
  if (begin+n > _extent) {
    dieif(n>tcache.extent(), "basic block size %ld bigger than cache %lu", n, _extent);
    die("tcache overflowed");
  }
  Header_t* bb = (Header_t*)&tcache.array[begin];
  memcpy(bb, wbb, n*sizeof(uint64_t));
  //dbmsg("added bb %8lx count=%d", bb->addr, bb->count);
  size_t h;
  do {
    h = hashfunction(bb->addr);
    bb->link = tcache.table[h];
  } while (!__sync_bool_compare_and_swap(&tcache.table[h], bb->link, begin));
  //dbmsg("add:  h=%ld, table[h]=%ld, begin=%ld, bb->link=%ld", h, table[h], begin, bb->link);
  return bb;
}

//    newval = (uint64_t*)bb - array;
    //    bb->next = tcache.table[h];
    //  } while (!__sync_bool_compare_and_swap(&tcache.table[h], bb->next, bb));
    //  } while (!__sync_bool_compare_and_swap(&tcache.table[h], bb->link, newval));

void Tcache_t::clear()
{
  memset((void*)array, 0, _extent*sizeof(size_t));
  //  memset((void*)table, 0, _hashsize*sizeof(Header_t**));
  _size = 0;
}





#ifdef DEBUG

pctrace_t Debug_t::get()
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  return trace[cursor];
}

void Debug_t::insert(pctrace_t pt)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor] = pt;
}

void Debug_t::insert(long pc, Insn_t i)
{
  cursor = (cursor+1) % PCTRACEBUFSZ;
  trace[cursor].pc    = pc;
  trace[cursor].i     = i;
  trace[cursor].val   = ~0l;
}

void Debug_t::addval(reg_t val)
{
  trace[cursor].val = val;
}

void Debug_t::print()
{
  for (int k=0; k<PCTRACEBUFSZ; k++) {
    pctrace_t t = get();
    Insn_t i = t.i;
    if (i.rd() != NOREG)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i.rd()], t.val);
    else if (attributes[i.opcode()] & ATTR_st)
      fprintf(stderr, "%4s[%016lx] ", reg_name[i.rs2()], t.val);
    else
      fprintf(stderr, "%4s[%16s] ", "", "");
    labelpc(t.pc);
    disasm(t.pc, &i, "");
    fprintf(stderr, "\n");
  }
}

#endif



#include "constants.h"


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
};
