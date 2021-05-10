



/* Use only this macro to advance program counter */
#define INCPC(bytes)	PC+=bytes

/* Taken branches */
#define RETURN(npc, sz)  { PC=npc; JUMP_ACTION(); break; }
#define JUMP(npc, sz)    { PC=npc; JUMP_ACTION(); break; }
#define GOTO(npc, sz)    { PC=npc; JUMP_ACTION(); break; }
#define CALL(npc, sz)    { Addr_t tgt=npc; IR(p->op_rd).l=PC+sz; PC=tgt; JUMP_ACTION(); break; }

/* Special instructions, may exit simulation */
#define STATS(cpu) { cpu->pc=PC; cpu->counter.insn_executed+=cpu->params.report-icount; STATS_ACTION(); }
#define UNSTATS(cpu) cpu->counter.insn_executed-=cpu->params.report-icount;
#define DOCSR(num, sz)  { STATS(cpu); proxy_csr(cpu, insn(PC), num); UNSTATS(cpu); }
#define ECALL(sz)       { STATS(cpu); if (proxy_ecall(cpu)) { cpu->state.mcause=8; INCPC(sz); goto stop_run; } UNSTATS(cpu); }
#define EBRK(num, sz)   { STATS(cpu); cpu->state.mcause= 3; goto stop_run; } /* no INCPC! */


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
#define SC_W(rd, r1, r2)      { amoB(cpu, "SC_W",      r1, r2); IR(rd).l=(lrsc_set==IR(r1).l) ? (*addrW(r1)=IR(r2).l, 0) : 1; amoE(cpu, rd); }
#define SC_L(rd, r1, r2)      { amoB(cpu, "SC_W",      r1, r2); IR(rd).l=(lrsc_set==IR(r1).l) ? (*addrL(r1)=IR(r2).l, 0) : 1; amoE(cpu, rd); }
#define FENCE(rd, r1, immed)  { amoB(cpu, "FENCE",     r1,  0); amoE(cpu, rd); }

// Define AMO instructions
#define AMOSWAP_W(rd, r1, r2) { amoB(cpu, "AMOSWAP_W", r1, r2); int  t=*addrW(r1); *addrW(r1) =IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOADD_W(rd, r1, r2)  { amoB(cpu, "AMOADD_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)+=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOXOR_W(rd, r1, r2)  { amoB(cpu, "AMOXOR_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)^=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOOR_W( rd, r1, r2)  { amoB(cpu, "AMOOR_W",   r1, r2); int  t=*addrW(r1); *addrW(r1)|=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOAND_W(rd, r1, r2)  { amoB(cpu, "AMOAND_W",  r1, r2); int  t=*addrW(r1); *addrW(r1)&=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }

#define AMOSWAP_L(rd, r1, r2) { amoB(cpu, "AMOSWAP_L", r1, r2); long t=*addrL(r1); *addrL(r1) =IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOADD_L(rd, r1, r2)  { amoB(cpu, "AMOADD_L",  r1, r2); long t=*addrL(r1); *addrL(r1)+=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOXOR_L(rd, r1, r2)  { amoB(cpu, "AMOXOR_L",  r1, r2); long t=*addrL(r1); *addrL(r1)^=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOOR_L( rd, r1, r2)  { amoB(cpu, "AMOOR_L",   r1, r2); long t=*addrL(r1); *addrL(r1)|=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }
#define AMOAND_L(rd, r1, r2)  { amoB(cpu, "AMOAND_L",  r1, r2); long t=*addrL(r1); *addrL(r1)&=IR(r2).l; IR(rd).l=t; amoE(cpu, rd); }

#define AMOMIN_W( rd, r1, r2) { amoB(cpu, "AMOMIN_W",  r1, r2);           int t1=*((          int*)addrW(r1)), t2=IR(r2).i;  if (t2 < t1) *addrW(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAX_W( rd, r1, r2) { amoB(cpu, "AMOMAX_W",  r1, r2);           int t1=*((          int*)addrW(r1)), t2=IR(r2).i;  if (t2 > t1) *addrW(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMINU_W(rd, r1, r2) { amoB(cpu, "AMOMINU_W", r1, r2); unsigned  int t1=*((unsigned  int*)addrW(r1)), t2=IR(r2).ui; if (t2 < t1) *addrW(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }
#define AMOMAXU_W(rd, r1, r2) { amoB(cpu, "AMOMAXU_W", r1, r2); unsigned  int t1=*((unsigned  int*)addrW(r1)), t2=IR(r2).ui; if (t2 > t1) *addrW(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }

#define AMOMIN_L( rd, r1, r2) { amoB(cpu, "AMOMIN_L",  r1, r2);          long t1=*((         long*)addrL(r1)), t2=IR(r2).l;  if (t2 < t1) *addrL(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMAX_L( rd, r1, r2) { amoB(cpu, "AMOMAX_L",  r1, r2);          long t1=*((         long*)addrL(r1)), t2=IR(r2).l;  if (t2 > t1) *addrL(r1)=t2; IR(rd).l =t1; amoE(cpu, rd); }
#define AMOMINU_L(rd, r1, r2) { amoB(cpu, "AMOMINU_L", r1, r2); unsigned long t1=*((unsigned long*)addrL(r1)), t2=IR(r2).ul; if (t2 < t1) *addrL(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }
#define AMOMAXU_L(rd, r1, r2) { amoB(cpu, "AMOMAXU_L", r1, r2); unsigned long t1=*((unsigned long*)addrL(r1)), t2=IR(r2).ul; if (t2 > t1) *addrL(r1)=t2; IR(rd).ul=t1; amoE(cpu, rd); }

