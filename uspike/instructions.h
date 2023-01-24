/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

class insnSpace_t {
  long _base;
  long _limit;
  long _entry;
  class Insn_t* predecoded;
public:  
  void loadelf(const char* elfname);
  long base() { return _base; }
  long limit() { return _limit; }
  long entry() { return _entry; }
  
  bool valid(long pc) { return _base<=pc && pc<_limit; }
  long index(long pc) { return (pc-_base)/2; }
  Insn_t at(long pc) { return predecoded[index(pc)]; }
  Insn_t* descr(long pc) { return &predecoded[index(pc)]; }
  uint32_t image(long pc) { return *(uint32_t*)(pc); }
  Insn_t set(long pc, Insn_t i) { predecoded[index(pc)] = i; return i; }
};

Insn_t decoder(int b, long pc);	// given bitpattern image of in struction

extern insnSpace_t code;

void substitute_cas(long lo, long hi);
int slabelpc(char* buf, long pc);
void labelpc(long pc, FILE* f =stderr);
int sdisasm(char* buf, long pc, Insn_t i);
void disasm(long pc, const char* end, FILE* f =stderr);
inline void disasm(long pc, FILE* f =stderr) { disasm(pc, "\n", f); }
