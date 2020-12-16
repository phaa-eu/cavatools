/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <stdint.h>

#include "caveat.h"
#include "opcodes.h"
#include "insn.h"


struct insnAttr_t insnAttr[] = {
#include "opcodes_attr.h"
};

struct insnSpace_t insnSpace;

const char* regName[] = {
  "zero", "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
  "s0",   "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
  "a6",   "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
  "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6",
  "ft0",  "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
  "fs0",  "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
  "fa6",  "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
  "fs8",  "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11",
  "NOT"
};


void insnSpace_init()
{
  dieif(insnSpace.base==0, "insnSpace_init() base, bound not initialized");
  assert(insnSpace.base < insnSpace.bound);
  
  long nelts = (insnSpace.bound - insnSpace.base) / 2;
  insnSpace.insn_array = (struct insn_t*)mmap(0, nelts*sizeof(struct insn_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(insnSpace.insn_array);
  memset(insnSpace.insn_array, 0, nelts*sizeof(struct insn_t));
  for (Addr_t pc=insnSpace.base; pc<insnSpace.bound; pc+=2)
    decode_instruction(&insnSpace.insn_array[(pc-insnSpace.base)/2], pc);
}


static inline struct insn_t fmtC(enum Opcode_t op, signed char rd, signed char rs1, int constant)
{
  struct insn_t i;
  i.op_code = op;
  i.op_rd = rd;
  i.op_rs1 = rs1;
  i.op_constant = constant;
  return i;
}

static inline struct insn_t fmtR(enum Opcode_t op, signed char rd, signed char rs1, signed char rs2, signed char rs3, short immed)
{
  struct insn_t i;
  i.op_code = op;
  i.op_rd = rd;
  i.op_rs1 = rs1;
  i.op.rs2 = rs2;
  i.op.rs3 = rs3;
  i.op.immed = immed;
  return i;
}

void decode_instruction( const struct insn_t* cp, Addr_t PC )
{
  struct insn_t* p = (struct insn_t*)cp;
  int ir = *((int*)(PC));
#include "decode_insn.h"
  p->op_code = Op_illegal;  // no match
  return;
}



int print_pc( long pc, FILE* f)
{
  const char* func;
  Addr_t offset;
  int known = 0;
  if (valid_pc(pc))  {
    long offset;
    known = find_pc(pc, &func, &offset);
    if (known)
      fprintf(f, "%28s+0x%-8lx ", func, offset);
    else
      fprintf(f, "%28s %-10s ", "UNKNOWN", "");
  }
  else
    fprintf(f, "%28s %-10s ", "INVALID", "");
  return known;
}


int format_insn(char* buf, const struct insn_t* p, Addr_t pc)
{
  if (!valid_pc(pc))
    return sprintf(buf, "%16lx  %8s\n", pc, "NOT TEXT");
  int n = sprintf(buf, shortOp(p->op_code) ? "%16lx      %04x  %-16s" : "%16lx  %08x  %-16s",
		  pc, *((unsigned short*)pc), insnAttr[p->op_code].name);
  buf += n;
  switch (p->op_code) {
#include "disasm_insn.h"
  }
  return n;
}

void print_insn(Addr_t pc, FILE* f)
{
  char buf[1024];
  format_insn(buf, insn(pc), pc);
  fprintf(f, "%s", buf);
}



void print_registers(struct reg_t reg[], FILE* f)
{
  char buf[1024];
  for (int i=0; i<64; i++) {
    fprintf(f, "%-4s: 0x%016lx  ", regName[i], reg[i].ul);
    if ((i+1) % 4 == 0)
      fprintf(f, "\n");
  }
}


static struct options_t* opt_ptr;
static const char* usage_ptr;

void help_exit()
{
  fprintf(stderr, "Usage: %s\n", usage_ptr);
  fprintf(stderr, "Options:  [choice, default (1st) value]\n");
  for (int i=0; opt_ptr[i].name; ++i)
    fprintf(stderr, "  %-14s  %s\n", opt_ptr[i].name, opt_ptr[i].h);
  exit(0);
}
  

int parse_options(struct options_t opt[], const char** argv, const char* usage)
{
  opt_ptr = opt;
  usage_ptr = usage;
  int numargs = 0;
  while (argv[numargs] && argv[numargs][0]=='-') {
    const char* arg = argv[numargs++];
    if (strcmp(arg, "--help") == 0)
      help_exit();
    for (int i=0; opt[i].name; ++i) {
      int len = strlen(opt[i].name);
      if (arg[len-1] == '=') {
	if (strncmp(arg, opt[i].name, len) == 0) {
	  *opt[i].v = (arg+len);
	  goto next_option;
	}
      }
      else {
	if (strcmp(arg, opt[i].name) == 0) {
	  *opt[i].f = 1;
	  goto next_option;
	}
      }
    }
    fprintf(stderr, "Illegal option %s\n", arg);
    exit(1);
  next_option:
    ;
  }
  return numargs;
}
