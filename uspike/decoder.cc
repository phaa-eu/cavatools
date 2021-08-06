#include "uspike.h"

Insn_t decoder(long pc)
{
  Insn_t i;
  int b = *(int32_t*)pc;
#define x( lo, len) ((b >> lo) & ((1 << len)-1))
#define xs(lo, len) (b << (32-lo-len) >> (32-len))
  
#include "decoder.h"

  i.op_code = Op_UNKNOWN;
  return i;
}
