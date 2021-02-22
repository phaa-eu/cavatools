/*
 *  Vector-no-Vector implementation of SPMV.
 *  (c) 2020 Peter Hsu
 */

//#define CAREFULLY

#ifndef SPMV_OP
#error Define SPMV_OP as += or -= then follow with subroutine header:
#error static inline double vnv_spmv_plus( long nnz, const int* c, const double* v, const double* x, double ac)
#error static inline double vnv_spmv_minus(long nnz, const int* c, const double* v, const double* x, double ac)
#endif

{
#ifdef CAREFULLY
  double sum = ac;
  long count = nnz;
#endif
  const int *cp = c;
  const double *vp = v;
  
  register 	int    c0, c1, c2, c3, c4;
  register 	double v0, v1, v2, v3, v4;
  register 	double x0, x1, x2, x3, x4;
  { /* Prolog */
    asm volatile("0: # Prolog stage 0"); {
      if (nnz > 0) { c0 = *cp++; }
    }
    asm volatile("1: # Prolog stage 1"); {
      if (nnz > 1) { c1 = *cp++; }
    }
    asm volatile("2: # Prolog stage 2"); {
      if (nnz > 0) { v0 = *vp++; x0 = x[c0]; }
      if (nnz > 2) { c2 = *cp++; }
    }
    asm volatile("3: # Prolog stage 3"); {
      if (nnz > 1) { v1 = *vp++; x1 = x[c1]; }
      if (nnz > 3) { c3 = *cp++; }
    }
  }
  /* Body */
  for (; nnz>5; nnz-=5) {
    asm volatile("4: # Body/epilog stage 4"); {
      { ac SPMV_OP v0*x0; }
      { v2 = *vp++; x2 = x[c2]; }
      { c4 = *cp++; }
    }
    asm volatile("0: # Body/epilog stage 0"); {
      { c0 = *cp++; }
      { ac SPMV_OP v1*x1; }
      { v3 = *vp++; x3 = x[c3]; }
    }
    asm volatile("1: # Body/epilog stage 1"); {
      { c1 = *cp++; }
      { ac SPMV_OP v2*x2; }
      { v4 = *vp++; x4 = x[c4]; }
    }
    asm volatile("2: # Body/epilog stage 2"); {
      { v0 = *vp++; x0 = x[c0]; }
      { c2 = *cp++; }
      { ac SPMV_OP v3*x3; }
    }
    asm volatile("3: # Body/epilog stage 3"); {
      { v1 = *vp++; x1 = x[c1]; }
      { c3 = *cp++; }
      { ac SPMV_OP v4*x4; }
    }
  }
  { /* Epilog */
    asm volatile("4: # Body/epilog stage 4"); {
      if (nnz > 0) { ac SPMV_OP v0*x0; }
      if (nnz > 2) { v2 = *vp++; x2 = x[c2]; }
      if (nnz > 4) { c4 = *cp++; }
    }
    asm volatile("0: # Body/epilog stage 0"); {
      if (nnz > 1) { ac SPMV_OP v1*x1; }
      if (nnz > 3) { v3 = *vp++; x3 = x[c3]; }
    }
    asm volatile("1: # Body/epilog stage 1"); {
      if (nnz > 2) { ac SPMV_OP v2*x2; }
      if (nnz > 4) { v4 = *vp++; x4 = x[c4]; }
    }
    asm volatile("2: # Body/epilog stage 2"); {
      if (nnz > 3) { ac SPMV_OP v3*x3; }
    }
    asm volatile("3: # Body/epilog stage 3"); {
      if (nnz > 4) { ac SPMV_OP v4*x4; }
    }
  }

#ifdef CAREFULLY

#define MISTAKES  10
#define Q(x)  #x
#define QUOTE(x) Q(x)

  {
    for (int k=0; k<count; k++)
      sum SPMV_OP v[k]*x[c[k]];
    if (sum != ac)
      abort();
  }
#endif
  return ac;
}
