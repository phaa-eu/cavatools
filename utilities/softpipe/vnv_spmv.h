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
  register int    c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10;
  register double v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10;
  register double x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10;
  if (nnz < 11) {  /* Single Pass */
      Stage(" 0:")  if (nnz>0) {  c0 = c[j+0]; }  
      Stage(" 1:")  if (nnz>1) {  c1 = c[j+1]; }  
      Stage(" 2:")  if (nnz>2) {  c2 = c[j+2]; }  
      Stage(" 3:")  if (nnz>3) {  c3 = c[j+3]; }  
      Stage(" 4:")  if (nnz>4) {  c4 = c[j+4]; }  
      Stage(" 5:")  if (nnz>5) {  c5 = c[j+5]; }  if (nnz>0) {  v0 = v[j+0];  x0 = x[c0]; }  
      Stage(" 6:")  if (nnz>6) {  c6 = c[j+6]; }  if (nnz>1) {  v1 = v[j+1];  x1 = x[c1]; }  
      Stage(" 7:")  if (nnz>7) {  c7 = c[j+7]; }  if (nnz>2) {  v2 = v[j+2];  x2 = x[c2]; }  
      Stage(" 8:")  if (nnz>8) {  c8 = c[j+8]; }  if (nnz>3) {  v3 = v[j+3];  x3 = x[c3]; }  
      Stage(" 9:")  if (nnz>9) {  c9 = c[j+9]; }  if (nnz>4) {  v4 = v[j+4];  x4 = x[c4]; }  
      Stage("10:")  if (nnz>5) {  v5 = v[j+5];  x5 = x[c5]; }  if (nnz>0) {  ac SPMV_OP v0*x0; }  
      Stage("11:")  if (nnz>6) {  v6 = v[j+6];  x6 = x[c6]; }  if (nnz>1) {  ac SPMV_OP v1*x1; }  
      Stage("12:")  if (nnz>7) {  v7 = v[j+7];  x7 = x[c7]; }  if (nnz>2) {  ac SPMV_OP v2*x2; }  
      Stage("13:")  if (nnz>8) {  v8 = v[j+8];  x8 = x[c8]; }  if (nnz>3) {  ac SPMV_OP v3*x3; }  
      Stage("14:")  if (nnz>9) {  v9 = v[j+9];  x9 = x[c9]; }  if (nnz>4) {  ac SPMV_OP v4*x4; }  
      Stage("15:")  if (nnz>5) {  ac SPMV_OP v5*x5; }  
      Stage("16:")  if (nnz>6) {  ac SPMV_OP v6*x6; }  
      Stage("17:")  if (nnz>7) {  ac SPMV_OP v7*x7; }  
      Stage("18:")  if (nnz>8) {  ac SPMV_OP v8*x8; }  
      Stage("19:")  if (nnz>9) {  ac SPMV_OP v9*x9; }  
  }
  else {
    { /* Prologue */
      Stage(" 0:")    c0 = c[j+0];
      Stage(" 1:")    c1 = c[j+1];
      Stage(" 2:")    c2 = c[j+2];
      Stage(" 3:")    c3 = c[j+3];
      Stage(" 4:")    c4 = c[j+4];
      Stage(" 5:")    c5 = c[j+5];  v0 = v[j+0];  x0 = x[c0];
      Stage(" 6:")    c6 = c[j+6];  v1 = v[j+1];  x1 = x[c1];
      Stage(" 7:")    c7 = c[j+7];  v2 = v[j+2];  x2 = x[c2];
      Stage(" 8:")    c8 = c[j+8];  v3 = v[j+3];  x3 = x[c3];
      Stage(" 9:")    c9 = c[j+9];  v4 = v[j+4];  x4 = x[c4];
      Stage("10:")    c10 = c[j+10];  v5 = v[j+5];  x5 = x[c5];  ac SPMV_OP v0*x0;
    }
    /*   Body */
    for (j+=11; j+11<nnz; j+=11) {
      Stage(" 0:")    c0 = c[j+0];  v6 = v[j+6-11];  x6 = x[c6];  ac SPMV_OP v1*x1;
      Stage(" 1:")    c1 = c[j+1];  v7 = v[j+7-11];  x7 = x[c7];  ac SPMV_OP v2*x2;
      Stage(" 2:")    c2 = c[j+2];  v8 = v[j+8-11];  x8 = x[c8];  ac SPMV_OP v3*x3;
      Stage(" 3:")    c3 = c[j+3];  v9 = v[j+9-11];  x9 = x[c9];  ac SPMV_OP v4*x4;
      Stage(" 4:")    c4 = c[j+4];  v10 = v[j+10-11];  x10 = x[c10];  ac SPMV_OP v5*x5;
      Stage(" 5:")    c5 = c[j+5];  v0 = v[j+0];  x0 = x[c0];  ac SPMV_OP v6*x6;
      Stage(" 6:")    c6 = c[j+6];  v1 = v[j+1];  x1 = x[c1];  ac SPMV_OP v7*x7;
      Stage(" 7:")    c7 = c[j+7];  v2 = v[j+2];  x2 = x[c2];  ac SPMV_OP v8*x8;
      Stage(" 8:")    c8 = c[j+8];  v3 = v[j+3];  x3 = x[c3];  ac SPMV_OP v9*x9;
      Stage(" 9:")    c9 = c[j+9];  v4 = v[j+4];  x4 = x[c4];  ac SPMV_OP v10*x10;
      Stage("10:")    c10 = c[j+10];  v5 = v[j+5];  x5 = x[c5];  ac SPMV_OP v0*x0;
    }
    { /* Epilogue */
      Stage(" 0:")  v6 = v[j+6-11];  x6 = x[c6];  ac SPMV_OP v1*x1;  if (nnz-j>0) {  c0 = c[j+0]; }  
      Stage(" 1:")  v7 = v[j+7-11];  x7 = x[c7];  ac SPMV_OP v2*x2;  if (nnz-j>1) {  c1 = c[j+1]; }  
      Stage(" 2:")  v8 = v[j+8-11];  x8 = x[c8];  ac SPMV_OP v3*x3;  if (nnz-j>2) {  c2 = c[j+2]; }  
      Stage(" 3:")  v9 = v[j+9-11];  x9 = x[c9];  ac SPMV_OP v4*x4;  if (nnz-j>3) {  c3 = c[j+3]; }  
      Stage(" 4:")  v10 = v[j+10-11];  x10 = x[c10];  ac SPMV_OP v5*x5;  if (nnz-j>4) {  c4 = c[j+4]; }  
      Stage(" 5:")  ac SPMV_OP v6*x6;  if (nnz-j>5) {  c5 = c[j+5]; }  if (nnz-j>0) {  v0 = v[j+0];  x0 = x[c0]; }  
      Stage(" 6:")  ac SPMV_OP v7*x7;  if (nnz-j>6) {  c6 = c[j+6]; }  if (nnz-j>1) {  v1 = v[j+1];  x1 = x[c1]; }  
      Stage(" 7:")  ac SPMV_OP v8*x8;  if (nnz-j>7) {  c7 = c[j+7]; }  if (nnz-j>2) {  v2 = v[j+2];  x2 = x[c2]; }  
      Stage(" 8:")  ac SPMV_OP v9*x9;  if (nnz-j>8) {  c8 = c[j+8]; }  if (nnz-j>3) {  v3 = v[j+3];  x3 = x[c3]; }  
      Stage(" 9:")  ac SPMV_OP v10*x10;  if (nnz-j>9) {  c9 = c[j+9]; }  if (nnz-j>4) {  v4 = v[j+4];  x4 = x[c4]; }  
      Stage("10:")  if (nnz-j>5) {  v5 = v[j+5];  x5 = x[c5]; }  if (nnz-j>0) {  ac SPMV_OP v0*x0; }  
      Stage("11:")  if (nnz-j>6) {  v6 = v[j+6-11];  x6 = x[c6]; }  if (nnz-j>1) {  ac SPMV_OP v1*x1; }  
      Stage("12:")  if (nnz-j>7) {  v7 = v[j+7-11];  x7 = x[c7]; }  if (nnz-j>2) {  ac SPMV_OP v2*x2; }  
      Stage("13:")  if (nnz-j>8) {  v8 = v[j+8-11];  x8 = x[c8]; }  if (nnz-j>3) {  ac SPMV_OP v3*x3; }  
      Stage("14:")  if (nnz-j>9) {  v9 = v[j+9-11];  x9 = x[c9]; }  if (nnz-j>4) {  ac SPMV_OP v4*x4; }  
      Stage("15:")  if (nnz-j>5) {  ac SPMV_OP v5*x5; }  
      Stage("16:")  if (nnz-j>6) {  ac SPMV_OP v6*x6; }  
      Stage("17:")  if (nnz-j>7) {  ac SPMV_OP v7*x7; }  
      Stage("18:")  if (nnz-j>8) {  ac SPMV_OP v8*x8; }  
      Stage("19:")  if (nnz-j>9) {  ac SPMV_OP v9*x9; }  
    }
  }

#ifdef CAREFULLY
  {
    for (int k=0; k<nnz; k++)
      sum SPMV_OP v[k]*x[c[k]];
    if (sum != ac) {
#define Q(x)  #x
#define QUOTE(x) Q(x)
      fprintf(stderr, "nnz=%ld sum=%12e ac=%12e vnv_spmv(" QUOTE(SPMV_OP) ")\n", nnz, sum, ac);
      exit(-1);
    }
  }
#endif
  return ac;
}
