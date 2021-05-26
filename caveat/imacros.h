
extern int amolock;		   /* 0=unlock, 1=lock with no waiters, 2=lock with waiters */
extern unsigned long lrsc_set;  // globally shared location for atomic lock

static inline void amoBegin(struct core_t* cpu, const char* name, int r1, int r2)
{
  int c = __sync_val_compare_and_swap(&amolock, 0, 1);
  if (c != 0) {
    do {
      if (c == 2 || __sync_val_compare_and_swap(&amolock, 1, 2) != 0)
	syscall(SYS_futex, &amolock, FUTEX_WAIT, 2, 0, 0, 0);
    } while ((c = __sync_val_compare_and_swap(&amolock, 0, 2)) != 0);
  }
}

static inline void amoEnd(struct core_t* cpu, int rd)
{
  if (__sync_fetch_and_sub(&amolock, 1) != 1) {
    amolock = 0;		/* should be atomic */
    syscall(SYS_futex, &amolock, FUTEX_WAKE, 1, 0, 0, 0);
  }
}


/* Use only this macro to advance program counter */
#define INCPC(bytes)	PC+=bytes

/* Special instructions, may exit simulation */
#define DOCSR(num, sz)  { proxy_csr(cpu, insn(PC), num);                            JUMP_ACTION();        }
#define ECALL(sz)       { __sync_fetch_and_or(&cpu->exceptions, ECALL_INSTRUCTION); JUMP_ACTION(); break; }
#define EBRK(num, sz)   { __sync_fetch_and_or(&cpu->exceptions, BREAKPOINT);        JUMP_ACTION(); break; }


// Memory reference instructions
#define LOAD_B( a, sz)  ( MEM_ACTION(a), *((          char*)(a)) )
#define LOAD_UB(a, sz)  ( MEM_ACTION(a), *((unsigned  char*)(a)) )
#define LOAD_H( a, sz)  ( MEM_ACTION(a), *((         short*)(a)) )
#define LOAD_UH(a, sz)  ( MEM_ACTION(a), *((unsigned short*)(a)) )
#define LOAD_W( a, sz)  ( MEM_ACTION(a), *((           int*)(a)) )
#define LOAD_UW(a, sz)  ( MEM_ACTION(a), *((unsigned   int*)(a)) )
#define LOAD_L( a, sz)  ( MEM_ACTION(a), *((          long*)(a)) )
#define LOAD_UL(a, sz)  ( MEM_ACTION(a), *((unsigned  long*)(a)) )
#define LOAD_F( a, sz)  ( MEM_ACTION(a), *((         float*)(a)) )
#define LOAD_D( a, sz)  ( MEM_ACTION(a), *((        double*)(a)) )

#define STORE_B(a, sz, v)  { MEM_ACTION(a), *((  char*)(a))=v; }
#define STORE_H(a, sz, v)  { MEM_ACTION(a), *(( short*)(a))=v; }
#define STORE_W(a, sz, v)  { MEM_ACTION(a), *((   int*)(a))=v; }
#define STORE_L(a, sz, v)  { MEM_ACTION(a), *((  long*)(a))=v; }
#define STORE_F(a, sz, v)  { MEM_ACTION(a), *(( float*)(a))=v; }
#define STORE_D(a, sz, v)  { MEM_ACTION(a), *((double*)(a))=v; }


// Define load reserve/store conditional emulation
#define addrW(rn)  (( int*)IR(rn).p)
#define addrL(rn)  ((long*)IR(rn).p)

#define LR_W(rd, r1)          { amoB(cpu, "LR_W",      r1,  0); lrsc_set=IR(r1).l; IR(rd).l=*addrW(r1); amoE(cpu, rd); }
#define LR_L(rd, r1)          { amoB(cpu, "LR_L",      r1,  0); lrsc_set=IR(r1).l; IR(rd).l=*addrL(r1); amoE(cpu, rd); }
//#define SC_W(rd, r1, r2)      { amoB(cpu, "SC_W",      r1, r2); IR(rd).l=(lrsc_set==IR(r1).l) ? (*addrW(r1)=IR(r2).l, 0) : 1; amoE(cpu, rd); }
//#define SC_L(rd, r1, r2)      { amoB(cpu, "SC_L",      r1, r2); IR(rd).l=(lrsc_set==IR(r1).l) ? (*addrL(r1)=IR(r2).l, 0) : 1; amoE(cpu, rd); }
#define SC_W(rd, r1, r2)      { amoB(cpu, "SC_W",      r1, r2); if (lrsc_set==IR(r1).l) { *addrW(r1)=IR(r2).l; IR(rd).l=0; } else IR(rd).l=1; amoE(cpu, rd); }
#define SC_L(rd, r1, r2)      { amoB(cpu, "SC_L",      r1, r2); if (lrsc_set==IR(r1).l) { *addrL(r1)=IR(r2).l; IR(rd).l=0; } else IR(rd).l=1; amoE(cpu, rd); }
#define FENCE(rd, r1, immed)  { amoB(cpu, "FENCE",     r1,  0); amoE(cpu, rd); }

// Define AMO instructions
#define AMOSWAP_W(rd, r1, r2) { amoB(cpu, "AMOSWAP_W", r1, r2); int  t=*addrW(r1); *addrW(r1) =IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOADD_W(rd, r1, r2)  { amoB(cpu, "AMOADD_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)+=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOXOR_W(rd, r1, r2)  { amoB(cpu, "AMOXOR_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)^=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOOR_W( rd, r1, r2)  { amoB(cpu, "AMOOR_W",   r1, r2); int  t=*addrW(r1); *addrW(r1)|=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOAND_W(rd, r1, r2)  { amoB(cpu, "AMOAND_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)&=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }

//#define AMOSWAP_W(rd, r1, r2) { amoB(cpu, "AMOSWAP_W", r1, r2); IR(rd).l = __sync_lock_test_and_set(addrW(r1), IR(r2).i); amoE(cpu, rd); }
//#define AMOADD_W(rd, r1, r2)  { amoB(cpu, "AMOADD_W",  r1, r2); IR(rd).l = __sync_fetch_and_add(    addrW(r1), IR(r2).i); amoE(cpu, rd); }
//#define AMOXOR_W(rd, r1, r2)  { amoB(cpu, "AMOXOR_W",  r1, r2); IR(rd).l = __sync_fetch_and_xor(    addrW(r1), IR(r2).i); amoE(cpu, rd); }
//#define AMOOR_W( rd, r1, r2)  { amoB(cpu, "AMOOR_W",   r1, r2); IR(rd).l = __sync_fetch_and_or(     addrW(r1), IR(r2).i); amoE(cpu, rd); }
//#define AMOAND_W(rd, r1, r2)  { amoB(cpu, "AMOAND_W",  r1, r2); IR(rd).l = __sync_fetch_and_and(    addrW(r1), IR(r2).i); amoE(cpu, rd); }

#define AMOSWAP_L(rd, r1, r2) { amoB(cpu, "AMOSWAP_L", r1, r2); long t=*addrL(r1); *addrL(r1) =IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOADD_L(rd, r1, r2)  { amoB(cpu, "AMOADD_L",  r1, r2); long t=*addrL(r1); *addrL(r1)+=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOXOR_L(rd, r1, r2)  { amoB(cpu, "AMOXOR_L",  r1, r2); long t=*addrL(r1); *addrL(r1)^=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOOR_L( rd, r1, r2)  { amoB(cpu, "AMOOR_L",   r1, r2); long t=*addrL(r1); *addrL(r1)|=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOAND_L(rd, r1, r2)  { amoB(cpu, "AMOAND_L",  r1, r2); long t=*addrL(r1); *addrL(r1)&=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }

//#define AMOSWAP_L(rd, r1, r2) { amoB(cpu, "AMOSWAP_L", r1, r2); IR(rd).l = __sync_lock_test_and_set(addrL(r1), IR(r2).l); amoE(cpu, rd); }
//#define AMOADD_L(rd, r1, r2)  { amoB(cpu, "AMOADD_L",  r1, r2); IR(rd).l = __sync_fetch_and_add(    addrL(r1), IR(r2).l); amoE(cpu, rd); }
//#define AMOXOR_L(rd, r1, r2)  { amoB(cpu, "AMOXOR_L",  r1, r2); IR(rd).l = __sync_fetch_and_xor(    addrL(r1), IR(r2).l); amoE(cpu, rd); }
//#define AMOOR_L( rd, r1, r2)  { amoB(cpu, "AMOOR_L",   r1, r2); IR(rd).l = __sync_fetch_and_or(     addrL(r1), IR(r2).l); amoE(cpu, rd); }
//#define AMOAND_L(rd, r1, r2)  { amoB(cpu, "AMOAND_L",  r1, r2); IR(rd).l = __sync_fetch_and_and(    addrL(r1), IR(r2).l); amoE(cpu, rd); }

#define AMOMIN_W( rd, r1, r2) { amoB(cpu, "AMOMIN_W",  r1, r2); int t1=*addrW(r1), t2=IR(r2).i;  if (IR(r2).i  <           t1) *addrW(r1)=IR(r2).i; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAX_W( rd, r1, r2) { amoB(cpu, "AMOMAX_W",  r1, r2); int t1=*addrW(r1), t2=IR(r2).i;  if (IR(r2).i  >           t1) *addrW(r1)=IR(r2).i; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMINU_W(rd, r1, r2) { amoB(cpu, "AMOMINU_W", r1, r2); int t1=*addrW(r1), t2=IR(r2).ui; if (IR(r2).ui < (unsigned)t1) *addrW(r1)=IR(r2).i; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAXU_W(rd, r1, r2) { amoB(cpu, "AMOMAXU_W", r1, r2); int t1=*addrW(r1), t2=IR(r2).ui; if (IR(r2).ui > (unsigned)t1) *addrW(r1)=IR(r2).i; IR(rd).l =t1; amoE(cpu, rd); }

//#define AMOMIN_W( rd, r1, r2) { amoB(cpu, "AMOMIN_W",  r1, r2);           int t1=*((          int*)addrW(r1)), t2=IR(r2).i;  if (t2 < t1) *addrW(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
//#define AMOMAX_W( rd, r1, r2) { amoB(cpu, "AMOMAX_W",  r1, r2);           int t1=*((          int*)addrW(r1)), t2=IR(r2).i;  if (t2 > t1) *addrW(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
//#define AMOMINU_W(rd, r1, r2) { amoB(cpu, "AMOMINU_W", r1, r2); unsigned  int t1=*((unsigned  int*)addrW(r1)), t2=IR(r2).ui; if (t2 < t1) *addrW(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }
//#define AMOMAXU_W(rd, r1, r2) { amoB(cpu, "AMOMAXU_W", r1, r2); unsigned  int t1=*((unsigned  int*)addrW(r1)), t2=IR(r2).ui; if (t2 > t1) *addrW(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }

#define AMOMIN_L( rd, r1, r2) { amoB(cpu, "AMOMIN_L",  r1, r2); long t1=*addrL(r1), t2=IR(r2).l;  if (IR(r2).l  <                t1) *addrL(r1)=IR(r2).l; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAX_L( rd, r1, r2) { amoB(cpu, "AMOMAX_L",  r1, r2); long t1=*addrL(r1), t2=IR(r2).l;  if (IR(r2).l  >                t1) *addrL(r1)=IR(r2).l; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMINU_L(rd, r1, r2) { amoB(cpu, "AMOMINU_L", r1, r2); long t1=*addrL(r1), t2=IR(r2).ul; if (IR(r2).ul < (unsigned long)t1) *addrL(r1)=IR(r2).l; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAXU_L(rd, r1, r2) { amoB(cpu, "AMOMAXU_L", r1, r2); long t1=*addrL(r1), t2=IR(r2).ul; if (IR(r2).ul > (unsigned long)t1) *addrL(r1)=IR(r2).l; IR(rd).l =t1; amoE(cpu, rd); }

//#define AMOMIN_L( rd, r1, r2) { amoB(cpu, "AMOMIN_L",  r1, r2);          long t1=*((         long*)addrL(r1)), t2=IR(r2).l;  if (t2 < t1) *addrL(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
//#define AMOMAX_L( rd, r1, r2) { amoB(cpu, "AMOMAX_L",  r1, r2);          long t1=*((         long*)addrL(r1)), t2=IR(r2).l;  if (t2 > t1) *addrL(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
//#define AMOMINU_L(rd, r1, r2) { amoB(cpu, "AMOMINU_L", r1, r2); unsigned long t1=*((unsigned long*)addrL(r1)), t2=IR(r2).ul; if (t2 < t1) *addrL(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }
//#define AMOMAXU_L(rd, r1, r2) { amoB(cpu, "AMOMAXU_L", r1, r2); unsigned long t1=*((unsigned long*)addrL(r1)), t2=IR(r2).ul; if (t2 > t1) *addrL(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }

