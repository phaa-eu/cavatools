/*
 *  Vector-no-Vector implementation of SPMV.
 *  (c) 2020 Peter Hsu
 */

#define CAREFULLY

#ifndef SPMV_OP
#error Define SPMV_OP as += or -= then follow with subroutine header:
#error static inline double vnv_spmv_plus( long nnz, const int* c, const double* v, const double* x, double ac)
#error static inline double vnv_spmv_minus(long nnz, const int* c, const double* v, const double* x, double ac)
#endif

#define Stage(label)  asm volatile (label);

{
#ifdef CAREFULLY
  double sum = ac;
#endif
  
  register int j=0;
  register int    c0, c1, c2;
  register double v0, v1, v2;
  register double x0, x1, x2;
  if (nnz < 3) {  /* Single Pass */
      Stage(" 0:")  if (nnz>0) {  c0 = c[j+0]; }  
      Stage(" 1:")  if (nnz>1) {  c1 = c[j+1]; }  if (nnz>0) {  v0 = v[j+0];  x0 = x[c0]; }  
      Stage(" 2:")  if (nnz>1) {  v1 = v[j+1];  x1 = x[c1]; }  if (nnz>0) {  ac SPMV_OP v0*x0; }  
      Stage(" 3:")  if (nnz>1) {  ac SPMV_OP v1*x1; }  
  }
  else {
    { /* Prologue */
      Stage(" 0:")    c0 = c[j+0];
      Stage(" 1:")    c1 = c[j+1];  v0 = v[j+0];  x0 = x[c0];
      Stage(" 2:")    c2 = c[j+2];  v1 = v[j+1];  x1 = x[c1];  ac SPMV_OP v0*x0;
    }
    /*   Body */
    for (j+=3; j+3<nnz; j+=3) {
      Stage(" 0:")    c0 = c[j+0];  v2 = v[j+2-3];  x2 = x[c2];  ac SPMV_OP v1*x1;
      Stage(" 1:")    c1 = c[j+1];  v0 = v[j+0];  x0 = x[c0];  ac SPMV_OP v2*x2;
      Stage(" 2:")    c2 = c[j+2];  v1 = v[j+1];  x1 = x[c1];  ac SPMV_OP v0*x0;
    }
    { /* Epilogue */
      Stage(" 0:")  v2 = v[j+2-3];  x2 = x[c2];  ac SPMV_OP v1*x1;  if (nnz-j>0) {  c0 = c[j+0]; }  
      Stage(" 1:")  ac SPMV_OP v2*x2;  if (nnz-j>1) {  c1 = c[j+1]; }  if (nnz-j>0) {  v0 = v[j+0];  x0 = x[c0]; }  
      Stage(" 2:")  if (nnz-j>1) {  v1 = v[j+1];  x1 = x[c1]; }  if (nnz-j>0) {  ac SPMV_OP v0*x0; }  
      Stage(" 3:")  if (nnz-j>1) {  ac SPMV_OP v1*x1; }  
    }
  }

#ifdef CAREFULLY
  {
    static long count = 0;
    count++;
    for (int k=0; k<nnz; k++)
      sum SPMV_OP v[k]*x[c[k]];
    if (sum != ac) {
#define Q(x)  #x
#define QUOTE(x) Q(x)
      fprintf(stderr, "count=%ld nnz=%ld sum=%12e ac=%12e vnv_spmv(" QUOTE(SPMV_OP) ")\n", count, nnz, sum, ac);
      exit(-1);
    }
  }
#endif
  return ac;
}
