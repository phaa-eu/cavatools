/*
 *  Vectors-no-Vector implementation of SPMV.
 *  (c) 2020 Peter Hsu
 */

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
  register int    c0, c1, c2, c3, c4, c5, c6, c7;
  register double v0, v1, v2, v3, v4, v5, v6, v7;
  register double x0, x1, x2, x3, x4, x5, x6, x7;
  if (nnz < 8) {  /* Single Pass */
      Stage(" 0:")  if (nnz>0) {  c0 = c[j+0]; }  
      Stage(" 1:")  if (nnz>1) {  c1 = c[j+1]; }  
      Stage(" 2:")  if (nnz>2) {  c2 = c[j+2]; }  
      Stage(" 3:")  if (nnz>3) {  c3 = c[j+3]; }  if (nnz>0) {  v0 = v[j+0];  x0 = x[c0]; }  
      Stage(" 4:")  if (nnz>4) {  c4 = c[j+4]; }  if (nnz>1) {  v1 = v[j+1];  x1 = x[c1]; }  
      Stage(" 5:")  if (nnz>5) {  c5 = c[j+5]; }  if (nnz>2) {  v2 = v[j+2];  x2 = x[c2]; }  
      Stage(" 6:")  if (nnz>6) {  c6 = c[j+6]; }  if (nnz>3) {  v3 = v[j+3];  x3 = x[c3]; }  
      Stage(" 7:")  if (nnz>4) {  v4 = v[j+4];  x4 = x[c4]; }  if (nnz>0) {  ac SPMV_OP v0*x0; }  
      Stage(" 8:")  if (nnz>5) {  v5 = v[j+5];  x5 = x[c5]; }  if (nnz>1) {  ac SPMV_OP v1*x1; }  
      Stage(" 9:")  if (nnz>6) {  v6 = v[j+6];  x6 = x[c6]; }  if (nnz>2) {  ac SPMV_OP v2*x2; }  
      Stage("10:")  if (nnz>3) {  ac SPMV_OP v3*x3; }  
      Stage("11:")  if (nnz>4) {  ac SPMV_OP v4*x4; }  
      Stage("12:")  if (nnz>5) {  ac SPMV_OP v5*x5; }  
      Stage("13:")  if (nnz>6) {  ac SPMV_OP v6*x6; }  
  }
  else {
    { /* Prologue */
      Stage(" 0:")    c0 = c[j+0];
      Stage(" 1:")    c1 = c[j+1];
      Stage(" 2:")    c2 = c[j+2];
      Stage(" 3:")    c3 = c[j+3];  v0 = v[j+0];  x0 = x[c0];
      Stage(" 4:")    c4 = c[j+4];  v1 = v[j+1];  x1 = x[c1];
      Stage(" 5:")    c5 = c[j+5];  v2 = v[j+2];  x2 = x[c2];
      Stage(" 6:")    c6 = c[j+6];  v3 = v[j+3];  x3 = x[c3];
      Stage(" 7:")    c7 = c[j+7];  v4 = v[j+4];  x4 = x[c4];  ac SPMV_OP v0*x0;
    }
    /*   Body */
    for (j+=8; j+8<nnz; j+=8) {
      Stage(" 0:")    c0 = c[j+0];  v5 = v[j+5-8];  x5 = x[c5];  ac SPMV_OP v1*x1;
      Stage(" 1:")    c1 = c[j+1];  v6 = v[j+6-8];  x6 = x[c6];  ac SPMV_OP v2*x2;
      Stage(" 2:")    c2 = c[j+2];  v7 = v[j+7-8];  x7 = x[c7];  ac SPMV_OP v3*x3;
      Stage(" 3:")    c3 = c[j+3];  v0 = v[j+0];  x0 = x[c0];  ac SPMV_OP v4*x4;
      Stage(" 4:")    c4 = c[j+4];  v1 = v[j+1];  x1 = x[c1];  ac SPMV_OP v5*x5;
      Stage(" 5:")    c5 = c[j+5];  v2 = v[j+2];  x2 = x[c2];  ac SPMV_OP v6*x6;
      Stage(" 6:")    c6 = c[j+6];  v3 = v[j+3];  x3 = x[c3];  ac SPMV_OP v7*x7;
      Stage(" 7:")    c7 = c[j+7];  v4 = v[j+4];  x4 = x[c4];  ac SPMV_OP v0*x0;
    }
    { /* Epilogue */
      Stage(" 0:")  v5 = v[j+5-8];  x5 = x[c5];  ac SPMV_OP v1*x1;  if (nnz-j>0) {  c0 = c[j+0]; }  
      Stage(" 1:")  v6 = v[j+6-8];  x6 = x[c6];  ac SPMV_OP v2*x2;  if (nnz-j>1) {  c1 = c[j+1]; }  
      Stage(" 2:")  v7 = v[j+7-8];  x7 = x[c7];  ac SPMV_OP v3*x3;  if (nnz-j>2) {  c2 = c[j+2]; }  
      Stage(" 3:")  ac SPMV_OP v4*x4;  if (nnz-j>3) {  c3 = c[j+3]; }  if (nnz-j>0) {  v0 = v[j+0];  x0 = x[c0]; }  
      Stage(" 4:")  ac SPMV_OP v5*x5;  if (nnz-j>4) {  c4 = c[j+4]; }  if (nnz-j>1) {  v1 = v[j+1];  x1 = x[c1]; }  
      Stage(" 5:")  ac SPMV_OP v6*x6;  if (nnz-j>5) {  c5 = c[j+5]; }  if (nnz-j>2) {  v2 = v[j+2];  x2 = x[c2]; }  
      Stage(" 6:")  ac SPMV_OP v7*x7;  if (nnz-j>6) {  c6 = c[j+6]; }  if (nnz-j>3) {  v3 = v[j+3];  x3 = x[c3]; }  
      Stage(" 7:")  if (nnz-j>4) {  v4 = v[j+4];  x4 = x[c4]; }  if (nnz-j>0) {  ac SPMV_OP v0*x0; }  
      Stage(" 8:")  if (nnz-j>5) {  v5 = v[j+5-8];  x5 = x[c5]; }  if (nnz-j>1) {  ac SPMV_OP v1*x1; }  
      Stage(" 9:")  if (nnz-j>6) {  v6 = v[j+6-8];  x6 = x[c6]; }  if (nnz-j>2) {  ac SPMV_OP v2*x2; }  
      Stage("10:")  if (nnz-j>3) {  ac SPMV_OP v3*x3; }  
      Stage("11:")  if (nnz-j>4) {  ac SPMV_OP v4*x4; }  
      Stage("12:")  if (nnz-j>5) {  ac SPMV_OP v5*x5; }  
      Stage("13:")  if (nnz-j>6) {  ac SPMV_OP v6*x6; }  
    }
  }

#ifdef CAREFULLY
  {
    for (int k=0; k<nnz; k++)
      sum SPMV_OP v[k]*x[c[k]];
    double epsilon = abs(sum-ac);
    if (epsilon > 1e-12) {
#define Q(x)  #x
#define QUOTE(x) Q(x)
      fprintf(stderr, "epsilon=%12e nnz=%ld sum=%12e ac=%12e vnv_spmv(" QUOTE(SPMV_OP) ")\n", epsilon, nnz, sum, ac);
      //exit(-1);
    }
  }
#endif
    
  return ac;
}
