/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unordered_map>
using namespace std;

long loadelf(const char* elfname);

class insnSpace_t {
  long _entry;
public:
  //  Isegment_t* tcache;		// binary translation cache
  //  Insn_t* instruction;		// array of translated instructions
  
  long _base;
  long _limit;
  class Insn_t* predecoded;
public:  
  long entry() { return _entry; }


  
  long base() { return _base; }
  long limit() { return _limit; }
#if 0  
  bool valid(long pc) { return _base<=pc && pc<_limit; }
  long index(long pc) { return (pc-_base)/2; }
  Insn_t at(long pc) { return predecoded[index(pc)]; }
  Insn_t* descr(long pc) { return &predecoded[index(pc)]; }
  uint32_t image(long pc) { return *(uint32_t*)(pc); }
  Insn_t set(long pc, Insn_t i) { predecoded[index(pc)] = i; return i; }
#endif
};

//extern insnSpace_t code;
//extern unordered_map<long, bb_header_t*> bbmap;

void substitute_cas(long lo, long hi);
int slabelpc(char* buf, long pc);
void labelpc(long pc, FILE* f =stderr);
int sdisasm(char* buf, long pc, Insn_t* i);
void disasm(long pc, Insn_t* i, const char* end, FILE* f =stderr);
inline void disasm(long pc, Insn_t* i, FILE* f =stderr) { disasm(pc, i, "\n", f); }
