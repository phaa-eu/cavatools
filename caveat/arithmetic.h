/*
  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

/* Convenience wrappers to simplify softfloat code sequences */

#define isBoxedF32(r) (isBoxedF64(r) && ((uint32_t)((r.v[0] >> 32) + 1) == 0))
#define unboxF32(r) (isBoxedF32(r) ? (uint32_t)r.v[0] : defaultNaNF32UI)
#define isBoxedF64(r) ((r.v[1] + 1) == 0)
#define unboxF64(r) (isBoxedF64(r) ? r.v[0] : defaultNaNF64UI)

inline float32_t f32(uint32_t v) { return { v }; }
inline float64_t f64(uint64_t v) { return { v }; }
inline float32_t f32(freg_t r) { return f32(unboxF32(r)); }
inline float64_t f64(freg_t r) { return f64(unboxF64(r)); }
inline float128_t f128(freg_t r) { return r; }

inline freg_t freg(float32_t f) { return { ((uint64_t)-1 << 32) | f.v, (uint64_t)-1 }; }
inline freg_t freg(float64_t f) { return { f.v, (uint64_t)-1 }; }
inline freg_t freg(float128_t f) { return f; }

// RISC-V things

#undef RM
#define RM ({ int rm = i->immed(); \
              if(rm == 7) rm = s.fcsr.f.rm; \
              if(rm > 4) die("Illegal instruction"); \
              rm; })
#define srm  softfloat_roundingMode = RM
#define sfx  s.fcsr.f.flags |= softfloat_exceptionFlags

#define F32_SIGN ((uint32_t)1 << 31)
#define F64_SIGN ((uint64_t)1 << 63)


// RISC-V sign-injection instructions

static inline float32_t fsgnj_s(float32_t a, float32_t b, bool n, bool x)
{
  return f32((a.v & ~F32_SIGN) | ((((x) ? a.v : (n) ? F32_SIGN : 0) ^ b.v) & F32_SIGN));
}

#define fsgnj64(a, b, n, x) \
  f64((f64(a).v & ~F64_SIGN) | ((((x) ? f64(a).v : (n) ? F64_SIGN : 0) ^ f64(b).v) & F64_SIGN))

// RISC-V compliant FP min/max

static inline float32_t fmin_s(float32_t f1, float32_t f2)
{
  bool less = f32_lt_quiet(f1, f2) || (f32_eq(f1, f2) && (f1.v & F32_SIGN));
  if (isNaNF32UI(f1.v) && isNaNF32UI(f2.v))
    return f32(defaultNaNF32UI);
  else
    return less || isNaNF32UI(f2.v) ? f1 : f2;
}

static inline float32_t fmax_s(float32_t f1, float32_t f2)
{
  bool greater = f32_lt_quiet(f2, f1) || (f32_eq(f2, f1) && (f2.v & F32_SIGN));
  if (isNaNF32UI(f1.v) && isNaNF32UI(f2.v))
    return f32(defaultNaNF32UI);
  else
    return greater || isNaNF32UI(f2.v) ? f1 : f2;
}

static inline float64_t fmin_d(float64_t f1, float64_t f2)
{
  bool less = f64_lt_quiet(f1, f2) || (f64_eq(f1, f2) && (f1.v & F64_SIGN));
  if (isNaNF64UI(f1.v) && isNaNF64UI(f2.v))
    return f64(defaultNaNF64UI);
  else
    return less || isNaNF64UI(f2.v) ? f1 : f2;
}

static inline float64_t fmax_d(float64_t f1, float64_t f2)
{
  bool greater = f64_lt_quiet(f2, f1) || (f64_eq(f2, f1) && (f2.v & F64_SIGN));
  if (isNaNF64UI(f1.v) && isNaNF64UI(f2.v))
    return f64(defaultNaNF64UI);
  else
    return greater || isNaNF64UI(f2.v) ? f1 : f2;
}

// RISCV-V classify (disappeared from SoftFloat-3e

static inline uint_fast16_t f32_classify( float32_t a )
{
  union ui32_f32 { uint32_t ui; float32_t f; } uA;
  uint_fast32_t uiA;

  uA.f = a;
  uiA = uA.ui;

  uint_fast16_t infOrNaN = expF32UI( uiA ) == 0xFF;
  uint_fast16_t subnormalOrZero = expF32UI( uiA ) == 0;
  bool sign = signF32UI( uiA );
  bool fracZero = fracF32UI( uiA ) == 0;
  bool isNaN = isNaNF32UI( uiA );
  bool isSNaN = softfloat_isSigNaNF32UI( uiA );

  return
    (  sign && infOrNaN && fracZero )          << 0 |
    (  sign && !infOrNaN && !subnormalOrZero ) << 1 |
    (  sign && subnormalOrZero && !fracZero )  << 2 |
    (  sign && subnormalOrZero && fracZero )   << 3 |
    ( !sign && infOrNaN && fracZero )          << 7 |
    ( !sign && !infOrNaN && !subnormalOrZero ) << 6 |
    ( !sign && subnormalOrZero && !fracZero )  << 5 |
    ( !sign && subnormalOrZero && fracZero )   << 4 |
    ( isNaN &&  isSNaN )                       << 8 |
    ( isNaN && !isSNaN )                       << 9;
}

static inline uint_fast16_t f64_classify( float64_t a )
{
    union ui64_f64 uA;
    uint_fast64_t uiA;

    uA.f = a;
    uiA = uA.ui;

    uint_fast16_t infOrNaN = expF64UI( uiA ) == 0x7FF;
    uint_fast16_t subnormalOrZero = expF64UI( uiA ) == 0;
    bool sign = signF64UI( uiA );
    bool fracZero = fracF64UI( uiA ) == 0;
    bool isNaN = isNaNF64UI( uiA );
    bool isSNaN = softfloat_isSigNaNF64UI( uiA );

    return
        (  sign && infOrNaN && fracZero )          << 0 |
        (  sign && !infOrNaN && !subnormalOrZero ) << 1 |
        (  sign && subnormalOrZero && !fracZero )  << 2 |
        (  sign && subnormalOrZero && fracZero )   << 3 |
        ( !sign && infOrNaN && fracZero )          << 7 |
        ( !sign && !infOrNaN && !subnormalOrZero ) << 6 |
        ( !sign && subnormalOrZero && !fracZero )  << 5 |
        ( !sign && subnormalOrZero && fracZero )   << 4 |
        ( isNaN &&  isSNaN )                       << 8 |
        ( isNaN && !isSNaN )                       << 9;
}




// Integer multiplication routines

static inline uint64_t mulhu(uint64_t a, uint64_t b)
{
  uint64_t t;
  uint32_t y1, y2, y3;
  uint64_t a0 = (uint32_t)a, a1 = a >> 32;
  uint64_t b0 = (uint32_t)b, b1 = b >> 32;

  t = a1*b0 + ((a0*b0) >> 32);
  y1 = t;
  y2 = t >> 32;

  t = a0*b1 + y1;
  y1 = t;

  t = a1*b1 + y2 + (t >> 32);
  y2 = t;
  y3 = t >> 32;

  return ((uint64_t)y3 << 32) | y2;
}

static inline int64_t mulh(int64_t a, int64_t b)
{
  int negate = (a < 0) != (b < 0);
  uint64_t res = mulhu(a < 0 ? -a : a, b < 0 ? -b : b);
  return negate ? ~res + (a * b == 0) : res;
}

static inline int64_t mulhsu(int64_t a, uint64_t b)
{
  int negate = a < 0;
  uint64_t res = mulhu(a < 0 ? -a : a, b);
  return negate ? ~res + (a * b == 0) : res;
}
