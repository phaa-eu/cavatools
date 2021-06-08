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
#include "cache.h"
#include "core.h"


struct insnAttr_t insnAttr[] = {
#include "opcodes_attr.h"
};

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

const char* ascii_color[] =
  {
   [0] = "\e[91m",		/* Red */
   [1] = "\e[92m",		/* Green */
   [2] = "\e[93m",		/* Yellow */
   [3] = "\e[94m",		/* Blue */
   [4] = "\e[95m",		/* Magenta */
   [5] = "\e[96m",		/* Cyan */
   [6] = "\e[97m",		/* Light Gray */
   [7] = "\e[90m",		/* Black */
  };


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


int format_pc(char* buf, int width, Addr_t pc)
{
  if (valid_pc(pc)) {
    const char* func;
    long offset;
    if (find_pc(pc, &func, &offset)) {
      snprintf(buf,    width, "%21s", func);
      snprintf(buf+21, width, "+%-5ld ", offset);
    }
    else
      snprintf(buf, width, "%21s %5s ", "UNKNOWN", "");
  }
  else
    snprintf(buf, width, "%21s %-5s ", "INVALID", "");
  return strlen(buf);
}
  
void print_pc( long pc, FILE* f)
{
  char buf[1024];
  format_pc(buf, 29, pc);
  fprintf(f, "%-28s", buf);
}



int format_insn(char* buf, const struct insn_t* p, Addr_t pc, unsigned int image)
{
  int n;
  if (shortOp(p->op_code))
    n = sprintf(buf, "%8lx     %04x %-16s", pc, image&0xffff, insnAttr[p->op_code].name);
  else
    n = sprintf(buf, "%8lx %08x %-16s", pc, image, insnAttr[p->op_code].name);
  buf += n;
  switch (p->op_code) {
#include "disasm_insn.h"
  }
  return n;
}

void print_insn(Addr_t pc, FILE* f)
{
  char buf[1024];
  format_insn(buf, insn(pc), pc, *((unsigned int*)pc));
  fprintf(f, "%s\n", buf);
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
  fprintf(stderr, "Usage : %s\n", usage);
  for (int i=0; opt[i].name; ++i) {
    int len = strlen(opt[i].name);
    fprintf(stderr, "  %-14s  %s ", opt[i].name, opt[i].h);
    if (opt[i].name[len-2] == '=')
      switch (opt[i].name[len-1]) {
      case 's':
	if (opt[i].ds)
	  fprintf(stderr, "[%s]\n",  opt[i].ds);
	else
	  fprintf(stderr, "[none]\n");
	break;
      case 'i':
	fprintf(stderr, "[%ld]\n", opt[i].di);
	break;
      default:
	fprintf(stderr, "Bad option %s\n", opt[i].name);
	exit(0);
      }
    else
      fprintf(stderr, "\n");
  }
  exit(0);
}


int parse_options(const char** argv)
{
  /* initialize default values */
  for (int i=0; opt[i].name; ++i) {
    int len = strlen(opt[i].name) - 1;
    if (opt[i].name[len-1] == '=')
      switch (opt[i].name[len]) {
      case 's':  *opt[i].s = opt[i].ds;  break;
      case 'i':  *opt[i].i = opt[i].di;  break;
      default:	fprintf(stderr, "Bad option %s\n", opt[i].name); exit(0);
      }
    else
      *opt[i].b = 0;		/* flag not given */
  }
  /* parse options */
  int numargs = 0;
  while (argv[numargs] && argv[numargs][0]=='-') {
    const char* arg = argv[numargs++];
    if (strcmp(arg, "--help") == 0)
      help_exit();
    for (int i=0; opt[i].name; ++i) {
      int len = strlen(opt[i].name) - 1;
      if (opt[i].name[len-1] == '=') {
	if (strncmp(opt[i].name, arg, len-1) == 0) {
	  switch (opt[i].name[len]) {
	  case 's':  *opt[i].s =     (arg+len);	break;
	  case 'i':  *opt[i].i = atoi(arg+len);	break;
	  }
	  goto next_option;
	}
      }
      else if (strcmp(arg, opt[i].name) == 0) {
	*opt[i].b = opt[i].bv;
	goto next_option;
      }
    }
    fprintf(stderr, "Illegal option %s\n", arg);
    exit(1);
  next_option:
    ;
  }
  //  for (int i=0; opt[i].name; ++i)
  //    fprintf(stderr, "%s = %ld\n", opt[i].name, *opt[i].i);
  return numargs;
}
