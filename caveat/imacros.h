/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
 */

//#define STORE(a, type, v)  { amoLock(a); lrscstate=0; *((type)(a))=v; amoUnlock(a); }

//#define STORE(a, type, v) { unsigned x; for (int t=0; t<MAXTRIES; t++) if ((x=_xbegin())==_XBEGIN_STARTED) break; if (x==_XBEGIN_STARTED) { lrscstate=0; *((type)(a))=v; _xend(); } else { amoLock(a); lrscstate=0; *((type)(a))=v; amoUnlock(a); } }



/* Use only this macro to advance program counter */
#define INCPC(bytes)	PC+=bytes

/* Special instructions, may exit simulation */
#define DOCSR(num, sz)  { proxy_csr(cpu, insn(PC), num);                            JUMP_ACTION();        }
#define ECALL(sz)       { __sync_fetch_and_or(&cpu->exceptions, ECALL_INSTRUCTION); JUMP_ACTION(); break; }
#define EBRK(num, sz)   { __sync_fetch_and_or(&cpu->exceptions, BREAKPOINT);        JUMP_ACTION(); break; }




#define LOAD(a, type)   *((type)(VA=(a)))
#define LOAD_B( a, sz)  LOAD(a,           char*)
#define LOAD_UB(a, sz)  LOAD(a, unsigned  char*)
#define LOAD_H( a, sz)  LOAD(a,          short*)
#define LOAD_UH(a, sz)  LOAD(a, unsigned short*)
#define LOAD_W( a, sz)  LOAD(a,            int*)
#define LOAD_UW(a, sz)  LOAD(a, unsigned   int*)
#define LOAD_L( a, sz)  LOAD(a,           long*)
#define LOAD_UL(a, sz)  LOAD(a, unsigned  long*)
#define LOAD_F( a, sz)  LOAD(a,          float*)
#define LOAD_D( a, sz)  LOAD(a,         double*)


#define AMOHASH		(1<<0)
#define AMOSHIFT	3
#define MAXTRIES	1

extern int amosemi[AMOHASH]; /* 0=unlock, 1=lock with no waiters, 2=lock with waiters */
extern unsigned long lrscstate;  // globally shared location for atomic lock

static inline void amoLock(Addr_t a)
{
  int* lock = &amosemi[(a>>AMOSHIFT)%AMOHASH];
  int c = __sync_val_compare_and_swap(lock, 0, 1);
  if (c != 0) {
    do {
      if (c == 2 || __sync_val_compare_and_swap(lock, 1, 2) != 0)
	syscall(SYS_futex, lock, FUTEX_WAIT, 2, 0, 0, 0);
    } while ((c = __sync_val_compare_and_swap(lock, 0, 2)) != 0);
  }
}

static inline void amoUnlock(Addr_t a)
{
  int* lock = &amosemi[(a>>AMOSHIFT)%AMOHASH];
  if (__sync_fetch_and_sub(lock, 1) != 1) {
    *lock = 0;		/* should be atomic */
    syscall(SYS_futex, &lock, FUTEX_WAKE, 1, 0, 0, 0);
  }
}


#define addrW(rn)  (( int*)IR(rn).p)
#define addrL(rn)  ((long*)IR(rn).p)
  
#ifdef DEBUG
#define amoSee(a)                   if (conf.amo) fprintf(stderr, "%sstore(%p)%s\n", color(cpu->tid), (void*)(a), nocolor);
#define amoShow(cpu, name, r1, r2)  if (conf.amo) fprintf(stderr, "%s%s(%p, %lx)", color(cpu->tid), name, IR(r1).p, IR(r2).l);
#define amoAfter(cpu, rd)           if (conf.amo) fprintf(stderr, "->%lx%s\n", IR(rd).l, nocolor);
#else
#define amoSee(a)
#define amoShow(cpu, name, r1, r2)
#define amoAfter(cpu, rd)
#endif

#define TM(a, action) { unsigned x; for (int t=0; t<MAXTRIES; t++) if ((x=_xbegin())==_XBEGIN_STARTED) break; if (x==_XBEGIN_STARTED) { action; _xend(); } else { amoLock(a); action; amoUnlock(a); } }
//#define TM(a, action) { action; }
  
//#define STORE(a, type, v) TM(a, amoSee(a); *((type)(VA=(a)))=v; lrscstate=0)
#define STORE(a, type, v)  *((type)(VA=(a)))=v
#define STORE_B(a, sz, v)  STORE(a,   char*, v)
#define STORE_H(a, sz, v)  STORE(a,  short*, v)
#define STORE_W(a, sz, v)  STORE(a,    int*, v)
#define STORE_L(a, sz, v)  STORE(a,   long*, v)
#define STORE_F(a, sz, v)  STORE(a,  float*, v)
#define STORE_D(a, sz, v)  STORE(a, double*, v)

//#define TMbegin() { unsigned x; for (int t=0; t<MAXTRIES; t++) if ((tms=_xbegin())==_XBEGIN_STARTED) break; if (x!=_XBEGIN_STARTED) goslow(); }
//#define TMend() { 

#define LR_W(rd, r1)          { amoShow(cpu, "LR_W",      r1,  0); TM(IR(r1).l, lrscstate=IR(r1).l; IR(rd).l=*addrW(r1)); amoAfter(cpu, rd); }
#define LR_L(rd, r1)          { amoShow(cpu, "LR_L",      r1,  0); TM(IR(r1).l, lrscstate=IR(r1).l; IR(rd).l=*addrL(r1)); amoAfter(cpu, rd); }
  
#define SC_W(rd, r1, r2)      { amoShow(cpu, "SC_W",      r1, r2); TM(IR(r1).l, if (lrscstate==IR(r1).l) { *addrW(r1)=IR(r2).i; IR(rd).l=0; } else IR(rd).l=1; lrscstate=0); amoAfter(cpu, rd); }
#define SC_L(rd, r1, r2)      { amoShow(cpu, "SC_L",      r1, r2); TM(IR(r1).l, if (lrscstate==IR(r1).l) { *addrL(r1)=IR(r2).l; IR(rd).l=0; } else IR(rd).l=1; lrscstate=0); amoAfter(cpu, rd); }
#define FENCE(rd, r1, immed)  { }

#define AMOSWAP_W(rd, r1, r2) { amoShow(cpu, "AMOSWAP_W", r1, r2); TM(IR(r1).l, int  t=*addrW(r1); *addrW(r1) =IR(r2).i; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOADD_W(rd, r1, r2)  { amoShow(cpu, "AMOADD_W",  r1, r2); TM(IR(r1).l, int  t=*addrW(r1); *addrW(r1)+=IR(r2).i; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOXOR_W(rd, r1, r2)  { amoShow(cpu, "AMOXOR_W",  r1, r2); TM(IR(r1).l, int  t=*addrW(r1); *addrW(r1)^=IR(r2).i; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOOR_W( rd, r1, r2)  { amoShow(cpu, "AMOOR_W",   r1, r2); TM(IR(r1).l, int  t=*addrW(r1); *addrW(r1)|=IR(r2).i; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOAND_W(rd, r1, r2)  { amoShow(cpu, "AMOAND_W",  r1, r2); TM(IR(r1).l, int  t=*addrW(r1); *addrW(r1)&=IR(r2).i; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }

#define AMOSWAP_L(rd, r1, r2) { amoShow(cpu, "AMOSWAP_L", r1, r2); TM(IR(r1).l, long t=*addrL(r1); *addrL(r1) =IR(r2).l; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOADD_L(rd, r1, r2)  { amoShow(cpu, "AMOADD_L",  r1, r2); TM(IR(r1).l, long t=*addrL(r1); *addrL(r1)+=IR(r2).l; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOXOR_L(rd, r1, r2)  { amoShow(cpu, "AMOXOR_L",  r1, r2); TM(IR(r1).l, long t=*addrL(r1); *addrL(r1)^=IR(r2).l; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOOR_L( rd, r1, r2)  { amoShow(cpu, "AMOOR_L",   r1, r2); TM(IR(r1).l, long t=*addrL(r1); *addrL(r1)|=IR(r2).l; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOAND_L(rd, r1, r2)  { amoShow(cpu, "AMOAND_L",  r1, r2); TM(IR(r1).l, long t=*addrL(r1); *addrL(r1)&=IR(r2).l; IR(rd).l=t; lrscstate=0); amoAfter(cpu, rd); }


#define AMOMIN_W( rd, r1, r2) { amoShow(cpu, "AMOMIN_W",  r1, r2); TM(IR(r1).l, int           t=*addrW(r1); if (IR(r2).i  < t) *addrW(r1)=IR(r2).i; IR(rd).l =t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOMAX_W( rd, r1, r2) { amoShow(cpu, "AMOMAX_W",  r1, r2); TM(IR(r1).l, int           t=*addrW(r1); if (IR(r2).i  > t) *addrW(r1)=IR(r2).i; IR(rd).l =t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOMINU_W(rd, r1, r2) { amoShow(cpu, "AMOMINU_W", r1, r2); TM(IR(r1).l, unsigned int  t=*addrW(r1); if (IR(r2).ui < t) *addrW(r1)=IR(r2).i; IR(rd).ul=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOMAXU_W(rd, r1, r2) { amoShow(cpu, "AMOMAXU_W", r1, r2); TM(IR(r1).l, unsigned int  t=*addrW(r1); if (IR(r2).ui > t) *addrW(r1)=IR(r2).i; IR(rd).ul=t; lrscstate=0); amoAfter(cpu, rd); }

#define AMOMIN_L( rd, r1, r2) { amoShow(cpu, "AMOMIN_L",  r1, r2); TM(IR(r1).l,          long t=*addrL(r1); if (IR(r2).l  < t) *addrL(r1)=IR(r2).i; IR(rd).l =t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOMAX_L( rd, r1, r2) { amoShow(cpu, "AMOMAX_L",  r1, r2); TM(IR(r1).l,          long t=*addrL(r1); if (IR(r2).l  > t) *addrL(r1)=IR(r2).i; IR(rd).l =t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOMINU_L(rd, r1, r2) { amoShow(cpu, "AMOMINU_L", r1, r2); TM(IR(r1).l, unsigned long t=*addrL(r1); if (IR(r2).ul < t) *addrL(r1)=IR(r2).i; IR(rd).ul=t; lrscstate=0); amoAfter(cpu, rd); }
#define AMOMAXU_L(rd, r1, r2) { amoShow(cpu, "AMOMAXU_L", r1, r2); TM(IR(r1).l, unsigned long t=*addrL(r1); if (IR(r2).ul > t) *addrL(r1)=IR(r2).i; IR(rd).ul=t; lrscstate=0); amoAfter(cpu, rd); }
