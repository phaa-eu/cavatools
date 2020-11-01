/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/


#define F32_SIGN (0x1  << 31)
#define F64_SIGN (0x1L << 63)
 
#define sgnj32(a, b, n, x)  ((FR(a).ui & ~F32_SIGN) | ((((x) ? FR(a).ui : (n) ? F32_SIGN : 0) ^ FR(b).ui) & F32_SIGN))
#define sgnj64(a, b, n, x)  ((FR(a).ul & ~F64_SIGN) | ((((x) ? FR(a).ul : (n) ? F64_SIGN : 0) ^ FR(b).ul) & F64_SIGN))



#ifdef SOFT_FP


#include "internals.h"
#include "softfloat_types.h"
#include "softfloat.h"

#define RM  cpu->state.fcsr.frm
#define SRM(rm)  softfloat_roundingMode=(rm==7?RM:rm);
#define SET_FPX  /* set_fp_exceptions() */

static inline float32_t defaultNaNF32UI() { union ui32_f32 u; u.ui=         0x7FC00000; return u.f; }
static inline float64_t defaultNaNF64UI() { union ui64_f64 u; u.ui=0x7FF8000000000000L; return u.f; }
#define f32_less(a, b) (f32_lt_quiet(a, b) || (f32_eq(a, b) && (a.v & F32_SIGN)))
#define f32_more(a, b) (f32_lt_quiet(b, a) || (f32_eq(b, a) && (b.v & F32_SIGN)))
static inline float32_t f32_min(float32_t a, float32_t b) { return (isNaNF32UI(a.v) && isNaNF32UI(b.v)) ? defaultNaNF32UI() : (f32_more(a, b) || isNaNF32UI(b.v)) ? a : b; }
static inline float32_t f32_max(float32_t a, float32_t b) { return (isNaNF32UI(a.v) && isNaNF32UI(b.v)) ? defaultNaNF32UI() : (f32_less(a, b) || isNaNF32UI(b.v)) ? a : b; }
#define f64_less(a, b) (f64_lt_quiet(a, b) || (f64_eq(a, b) && (a.v & F64_SIGN)))
#define f64_more(a, b) (f64_lt_quiet(b, a) || (f64_eq(b, a) && (b.v & F64_SIGN)))
static inline float64_t f64_min(float64_t a, float64_t b) { return (isNaNF64UI(a.v) && isNaNF64UI(b.v)) ? defaultNaNF64UI() : (f64_more(a, b) || isNaNF64UI(b.v)) ? a : b; }
static inline float64_t f64_max(float64_t a, float64_t b) { return (isNaNF64UI(a.v) && isNaNF64UI(b.v)) ? defaultNaNF64UI() : (f64_less(a, b) || isNaNF64UI(b.v)) ? a : b; }


#else


#include <fenv.h>
static long tmp_rm;
#define SRM(rm)  if (rm!=7) { tmp_rm=fegetround(); fesetround(riscv_to_c_rm(rm)); }
#define RRM(rm)  if(rm!=7) fesetround(tmp_rm);
#define SET_FPX  ;/* set_fp_exceptions() */

static inline long riscv_to_c_rm(long rm)
{
  switch (rm) {
  case /* RNE */ 0x0:  return FE_TONEAREST;
  case /* RTZ */ 0x1:  return FE_TOWARDZERO;
  case /* RDN */ 0x2:  return FE_DOWNWARD;
  case /* RUP */ 0x3:  return FE_UPWARD;
  default:  abort();
  }
}

  
#endif



// Following copied from Spike

static inline unsigned long mulhu(unsigned long a, unsigned long b)
{
  unsigned long t;
  unsigned int y1, y2, y3;
  unsigned long a0 = (unsigned int)a, a1 = a >> 32;
  unsigned long b0 = (unsigned int)b, b1 = b >> 32;

  t = a1*b0 + ((a0*b0) >> 32);
  y1 = t;
  y2 = t >> 32;

  t = a0*b1 + y1;
  y1 = t;

  t = a1*b1 + y2 + (t >> 32);
  y2 = t;
  y3 = t >> 32;

  return ((unsigned long)y3 << 32) | y2;
}

static inline long mulh(long a, long b)
{
  int negate = (a < 0) != (b < 0);
  unsigned long res = mulhu(a < 0 ? -a : a, b < 0 ? -b : b);
  return negate ? ~res + (a * b == 0) : res;
}

static inline long mulhsu(long a, unsigned long b)
{
  int negate = a < 0;
  unsigned long res = mulhu(a < 0 ? -a : a, b);
  return negate ? ~res + (a * b == 0) : res;
}
