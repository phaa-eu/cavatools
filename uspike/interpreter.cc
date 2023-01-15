/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "options.h"
#include "uspike.h"
#include "instructions.h"
#include "mmu.h"
#include "hart.h"


inline uint64_t mulhu(uint64_t a, uint64_t b)
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

inline int64_t mulh(int64_t a, int64_t b)
{
  int negate = (a < 0) != (b < 0);
  uint64_t res = mulhu(a < 0 ? -a : a, b < 0 ? -b : b);
  return negate ? ~res + (a * b == 0) : res;
}

inline int64_t mulhsu(int64_t a, uint64_t b)
{
  int negate = a < 0;
  uint64_t res = mulhu(a < 0 ? -a : a, b);
  return negate ? ~res + (a * b == 0) : res;
}









#define THREAD_STACK_SIZE  (1<<14)

option<>     conf_isa("isa",		"rv64imafdcv",			"RISC-V ISA string");
option<>     conf_vec("vec",		"vlen:128,elen:64,slen:128",	"Vector unit parameters");
option<long> conf_stat("stat",		100,				"Status every M instructions");
option<bool> conf_ecall("ecall",	false, true,			"Show system calls");
option<bool> conf_quiet("quiet",	false, true,			"No status report");


struct syscall_map_t {
  int sysnum;
  const char* name;
};

struct syscall_map_t rv_to_host[] = {
#include "ecall_nums.h"  
};
const int highest_ecall_num = HIGHEST_ECALL_NUM;

void status_report()
{
  if (conf_quiet)
    return;
  double realtime = elapse_time();
  fprintf(stderr, "\r\33[2K%12ld insns %3.1fs %3.1f MIPS ", hart_t::total_count(), realtime, hart_t::total_count()/1e6/realtime);
  if (hart_t::threads() <= 16) {
    char separator = '(';
    for (hart_t* p=hart_t::list(); p; p=p->next()) {
      fprintf(stderr, "%c%1ld%%", separator, 100*p->executed()/hart_t::total_count());
      separator = ',';
    }
    fprintf(stderr, ")");
  }
  else if (hart_t::threads() > 1)
    fprintf(stderr, "(%d cores)", hart_t::threads());
}

/* RISCV-V clone() system call arguments not same as X86_64:
   a0 = flags
   a1 = child_stack
   a2 = parent_tidptr
   a3 = tls
   a4 = child_tidptr
*/

void show(hart_t* cpu, long pc, FILE* f)
{
  Insn_t i = code.at(pc);
  int rn = i.rd()==NOREG ? i.rs2() : i.rd();
  long rv = cpu->read_reg(rn);
  if (rn != NOREG)
    fprintf(stderr, "%6ld: %4s[%16lx] ", cpu->tid(), reg_name[rn], rv);
  else
    fprintf(stderr, "%6ld: %4s[%16s] ", cpu->tid(), "", "");
  labelpc(pc);
  disasm(pc);
}

template<class T> bool hart_t::cas(long pc)
{
  Insn_t i = code.at(pc);
  T* ptr = (T*)read_reg(i.rs1());
  T expect  = read_reg(i.rs2());
  T replace = read_reg(i.rs3());
  T oldval = __sync_val_compare_and_swap(ptr, expect, replace);
  write_reg(code.at(pc+4).rs1(), oldval);
  if (oldval == expect)  write_reg(i.rd(), 0);	/* sc was successful */
  return oldval == expect;
}

extern long (*golden[])(long pc, mmu_t& MMU, class processor_t* p);

#define wrd(e)	xrf[i.rd()]=(e)
#define r1	xrf[i.rs1()]
#define r2	xrf[i.rs2()]
#define imm	i.immed()

#if 0
#define wfd(e)	STATE.FPR.write((i.rd()-FPREG), freg(e)), dirty_fp_state
#define f1	READ_FREG(i.rs1()-FPREG)
#define f2	READ_FREG(i.rs2()-FPREG)
//#define f3	READ_FREG(i.rs3()-FPREG)
#define f3	FRS3
#endif

#define wfd(e)	frf[i.rd()-FPREG] = freg(e)
#define f1	frf[i.rs1()-FPREG]
#define f2	frf[i.rs2()-FPREG]
#define f3	frf[i.rs3()-FPREG]

#define MMU	(*mmu())
#define wpc(npc)  pc=MMU.jump_model(npc, pc)
#define serialize()

bool hart_t::interpreter(long how_many)
{
  //  processor_t* p = spike();
  //  long* xreg = reg_file();
  long pc = read_pc();
  long insns = 0;
#ifdef DEBUG
  long oldpc;
#endif
  do {
#ifdef DEBUG
    dieif(!code.valid(pc), "Invalid PC %lx, oldpc=%lx", pc, oldpc);
    oldpc = pc;
    debug.insert(executed()+insns+1, pc);
#endif

    mmu()->insn_model(pc);
    Insn_t i = code.at(pc);

#if 0
    labelpc(pc);
    disasm(pc);
#endif
    
    //    insn_t insn = (long)(*(int32_t*)pc);
    switch ((Opcode_t)i.opcode()) {
    case Op_ZERO:  die("Op_ZERO opcode");
      //    case Op_ZERO:  diesegv();

    case Op_c_addi4spn:  wrd(r1+imm); pc+=2; break;
    case Op_c_fld: wfd(f64(MMU.load_uint64(r1+imm)));  pc+=2; break;
    case Op_c_lw:  wrd(MMU.load_int32(r1+imm)); pc+=2; break;
    case Op_c_ld:  wrd(MMU.load_int64(r1+imm)); pc+=2; break;
    case Op_c_fsd: MMU.store_uint64(r1+imm, f2.v[0]); pc+=2; break;
    case Op_c_sw:  MMU.store_int32(r1+imm, r2); pc+=2; break;
    case Op_c_sd:  MMU.store_int64(r1+imm, r2); pc+=2; break;
      
    case Op_c_addi:  wrd(r1 + imm); pc+=2; break;
    case Op_c_addiw:  wrd(int32_t(r1) + int32_t(imm)); pc+=2; break;
    case Op_c_li:  wrd(imm); pc+=2; break;
    case Op_c_addi16sp:  wrd(r1+imm); pc+=2; break;
    case Op_c_lui:  wrd(imm); pc+=2; break;
    case Op_c_srli:  wrd(uint64_t(r1) >> imm); pc+=2; break;
    case Op_c_srai:  wrd( int64_t(r1) >> imm); pc+=2; break;
    case Op_c_andi:  wrd(r1 & imm); pc+=2; break;
    case Op_c_sub:   wrd(r1 - r2); pc+=2; break;
    case Op_c_xor:   wrd(r1 ^ r2); pc+=2; break;
    case Op_c_or:    wrd(r1 | r2); pc+=2; break;
    case Op_c_and:   wrd(r1 & r2); pc+=2; break;
    case Op_c_subw:  wrd(int32_t(r1) - int32_t(r2)); pc+=2; break;
    case Op_c_addw:  wrd(int32_t(r1) + int32_t(r2)); pc+=2; break;
    case Op_c_j:     wpc(pc+imm); break; pc+=2; break;
    case Op_c_beqz:  if (r1==0) { wpc(pc+imm); break; }; pc+=2; break;
    case Op_c_bnez:  if (r1!=0) { wpc(pc+imm); break; }; pc+=2; break;
      
    case Op_c_slli:  wrd(uint64_t(r1) << imm); pc+=2; break;
    case Op_c_fldsp: wfd(f64(MMU.load_uint64(r1+imm)));  pc+=2; break;
    case Op_c_lwsp:  wrd(MMU.load_int32(r1+imm)); pc+=2; break;
    case Op_c_ldsp:  wrd(MMU.load_int64(r1+imm)); pc+=2; break;
    case Op_c_jr:  wpc(r1); break; pc+=2; break;
    case Op_c_mv:  wrd(r2); pc+=2; break;
    case Op_c_ebreak:  die("breakpoint not implemented");
    case Op_c_jalr:  { long t=pc+2; wpc(r1); wrd(t); break; }; pc+=2; break;
    case Op_c_add:  wrd(r1 + r2); pc+=2; break;
    case Op_c_fsdsp: MMU.store_uint64(r1+imm, f2.v[0]); pc+=2; break;
    case Op_c_swsp:  MMU.store_int32(r1+imm, r2); pc+=2; break;
    case Op_c_sdsp:  MMU.store_int64(r1+imm, r2); pc+=2; break;
      
    case Op_lui:  wrd(imm); pc+=4; break;
    case Op_auipc:  wrd(pc + imm); pc+=4; break;
    case Op_jal:  { long t=pc+4; wpc(pc+imm);     wrd(t); break; }; pc+=4; break;
    case Op_jalr:  { long t=pc+4; wpc((r1+imm)&~1L); wrd(t); break; }; pc+=4; break;

    case Op_beq:  if ( int64_t(r1)== int64_t(r2)) { wpc(pc+imm); break; }; pc+=4; break;
    case Op_bne:  if ( int64_t(r1)!= int64_t(r2)) { wpc(pc+imm); break; }; pc+=4; break;
    case Op_blt:  if ( int64_t(r1)<  int64_t(r2)) { wpc(pc+imm); break; }; pc+=4; break;
    case Op_bge:  if ( int64_t(r1)>= int64_t(r2)) { wpc(pc+imm); break; }; pc+=4; break;
    case Op_bltu:  if (uint64_t(r1)< uint64_t(r2)) { wpc(pc+imm); break; }; pc+=4; break;
    case Op_bgeu:  if (uint64_t(r1)>=uint64_t(r2)) { wpc(pc+imm); break; }; pc+=4; break;

    case Op_lb:  wrd(MMU.load_int8  (r1+imm)); pc+=4; break;
    case Op_lh:  wrd(MMU.load_int16 (r1+imm)); pc+=4; break;
    case Op_lw:  wrd(MMU.load_int32 (r1+imm)); pc+=4; break;
    case Op_lbu:  wrd(MMU.load_uint8 (r1+imm)); pc+=4; break;
    case Op_lhu:  wrd(MMU.load_uint16(r1+imm)); pc+=4; break;
    case Op_sb:  MMU.store_int8 (r1+imm, r2); pc+=4; break;
    case Op_sh:  MMU.store_int16(r1+imm, r2); pc+=4; break;
    case Op_sw:  MMU.store_int32(r1+imm, r2); pc+=4; break;

    case Op_addi:  wrd(r1 + imm); pc+=4; break;
    case Op_slti:  wrd( int64_t(r1) <  int64_t(imm)); pc+=4; break;
    case Op_sltiu:  wrd(uint64_t(r1) < uint64_t(imm)); pc+=4; break;
    case Op_xori:  wrd(r1 ^ imm); pc+=4; break;
    case Op_ori:  wrd(r1 | imm); pc+=4; break;
    case Op_andi:  wrd(r1 & imm); pc+=4; break;
    case Op_slli:  wrd(uint64_t(r1) << imm); pc+=4; break;
    case Op_srli:  wrd(uint64_t(r1) >> imm); pc+=4; break;
    case Op_srai:  wrd( int64_t(r1) >> imm); pc+=4; break;

    case Op_add:  wrd(r1 + r2); pc+=4; break;
    case Op_sub:  wrd(r1 - r2); pc+=4; break;
    case Op_sll:  wrd(uint64_t(r1) << uint64_t(r2)); pc+=4; break;
    case Op_slt:  wrd( int64_t(r1) <   int64_t(r2)); pc+=4; break;
    case Op_sltu:  wrd(uint64_t(r1) <  uint64_t(r2)); pc+=4; break;
    case Op_xor:  wrd(r1 ^ r2); pc+=4; break;
    case Op_srl:  wrd(uint64_t(r1) >> uint64_t(r2)); pc+=4; break;
    case Op_sra:  wrd( int64_t(r1) >>  int64_t(r2)); pc+=4; break;
    case Op_or:  wrd(r1 | r2); pc+=4; break;
    case Op_and:  wrd(r1 & r2); pc+=4; break;

    case Op_fence:                       pc+=4; break; 
    case Op_fence_i: MMU.flush_icache(); pc+=4; break; 
    case Op_ecall:   proxy_ecall(insns); pc+=4; break;
      //    case Op_ecall:  write_pc(pc); throw trap_user_ecall();
    case Op_ebreak:  die("breakpoint not implemented");
      
#define r1n0  i.rs1()!=0
    case Op_csrrw:   { int csr=imm;       long old=get_csr(csr, r1n0); if (r1n0) set_csr(csr,                r1      ); wrd(sext_xlen(old)); serialize(); pc+=4; break; }
    case Op_csrrwi:  { int csr=imm&0xFFF; long old=get_csr(csr, r1n0); if (r1n0) set_csr(csr,                imm>>12 ); wrd(sext_xlen(old)); serialize(); pc+=4; break; }
    case Op_csrrs:   { int csr=imm;       long old=get_csr(csr, r1n0); if (r1n0) set_csr(csr, old |          r1      ); wrd(sext_xlen(old)); serialize(); pc+=4; break; }
    case Op_csrrsi:  { int csr=imm&0xFFF; long old=get_csr(csr, r1n0); if (r1n0) set_csr(csr, old |          imm>>12 ); wrd(sext_xlen(old)); serialize(); pc+=4; break; }
    case Op_csrrc:   { int csr=imm;       long old=get_csr(csr, r1n0); if (r1n0) set_csr(csr, old & ~        r1      ); wrd(sext_xlen(old)); serialize(); pc+=4; break; }
    case Op_csrrci:  { int csr=imm&0xFFF; long old=get_csr(csr, r1n0); if (r1n0) set_csr(csr, old & ~(long)(imm>>12)); wrd(sext_xlen(old)); serialize(); pc+=4; break; }
      
    case Op_lwu: wrd(MMU.load_uint32(r1+imm)); pc+=4; break;
    case Op_ld:  wrd(MMU.load_int64 (r1+imm)); pc+=4; break;
    case Op_sd:  MMU.store_int64(r1+imm, r2); pc+=4; break;

    case Op_addiw:  wrd( int32_t(r1) +  int32_t(imm)); pc+=4; break;
    case Op_slliw:  wrd(uint32_t(r1) << imm); pc+=4; break;
    case Op_srliw:  wrd(uint32_t(r1) >> imm); pc+=4; break;
    case Op_sraiw:  wrd( int32_t(r1) >> imm); pc+=4; break;
      
    case Op_addw:  wrd( int32_t(r1) +   int32_t(r2)); pc+=4; break;
    case Op_subw:  wrd( int32_t(r1) -   int32_t(r2)); pc+=4; break;
    case Op_sllw:  wrd(uint32_t(r1) << uint32_t(r2)); pc+=4; break;
    case Op_srlw:  wrd(uint32_t(r1) >> uint32_t(r2)); pc+=4; break;
    case Op_sraw:  wrd( int32_t(r1) >>  int32_t(r2)); pc+=4; break;

    case Op_mul:   wrd(r1 * r2);         pc+=4; break;
    case Op_mulh:  wrd(mulh(  r1,  r2)); pc+=4; break;
    case Op_mulhsu:wrd(mulhsu(r1,  r2)); pc+=4; break;
    case Op_mulhu: wrd(mulhu( r1,  r2)); pc+=4; break;
    case Op_div:   wrd(r2==0 ?  INT64_MAX : (r1==INT64_MIN && r2==-1) ? r1 : r1/r2); pc+=4; break;
    case Op_divu:  wrd(r2==0 ? UINT64_MAX : (uint64_t)r1/(uint64_t)r2             ); pc+=4; break;
    case Op_rem:   wrd(r2==0 ?  INT64_MAX : (r1==INT64_MIN && r2==-1) ? r1 : r1%r2); pc+=4; break;
    case Op_remu:  wrd(r2==0 ? UINT64_MAX : (uint64_t)r1%(uint64_t)r2             ); pc+=4; break;

    case Op_mulw:  wrd((int32_t)r1 * (int32_t)r2);                                   pc+=4; break;
    case Op_divw:  wrd(r2==0 ? UINT64_MAX : ( int32_t)r1/( int32_t)r2             ); pc+=4; break;
    case Op_divuw: wrd(r2==0 ? UINT64_MAX : (uint32_t)r1/(uint32_t)r2             ); pc+=4; break;
    case Op_remw:  wrd(r2==0 ? UINT64_MAX : ( int32_t)r1%( int32_t)r2             ); pc+=4; break;
    case Op_remuw: wrd(r2==0 ? UINT64_MAX : (uint32_t)r1%(uint32_t)r2             ); pc+=4; break;
      
    case Op_cas12_w:  if (!cas<int32_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=12; break;
    case Op_cas12_d:  if (!cas<int64_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=12; break;
    case Op_cas10_w:  if (!cas<int32_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=10; break;
    case Op_cas10_d:  if (!cas<int64_t>(pc)) { wpc(pc+code.at(pc+4).immed()+4); break; }; pc+=10; break;


#undef RM
#define RM ({ int rm = i.immed(); \
              if(rm == 7) rm = fcsr.f.rm; \
              if(rm > 4) die("Illegal instruction"); \
              rm; })
#define srm  softfloat_roundingMode=RM
#define sfx  fcsr.f.flags |= softfloat_exceptionFlags;


#define fmin_s_body() \
      { \
	bool less = f32_lt_quiet(f32(f1), f32(f2)) || (f32_eq(f32(f1), f32(f2)) && (f32(f1).v & F32_SIGN)); \
	if (isNaNF32UI(f32(f1).v) && isNaNF32UI(f32(f2).v)) \
	  wfd(f32(defaultNaNF32UI)); \
	else \
	  wfd(less || isNaNF32UI(f32(f2).v) ? f1 : f2); \
      }

#define fmax_s_body() \
      { \
	bool greater = f32_lt_quiet(f32(f2), f32(f1)) || (f32_eq(f32(f2), f32(f1)) && (f32(f2).v & F32_SIGN)); \
	if (isNaNF32UI(f32(f1).v) && isNaNF32UI(f32(f2).v)) \
	  wfd(f32(defaultNaNF32UI)); \
	else \
	  wfd(greater || isNaNF32UI(f32(f2).v) ? f1 : f2); \
      }

#define fmin_d_body() \
      { \
	bool less = f64_lt_quiet(f64(f1), f64(f2)) || (f64_eq(f64(f1), f64(f2)) && (f64(f1).v & F64_SIGN)); \
	if (isNaNF64UI(f64(f1).v) && isNaNF64UI(f64(f2).v)) \
	  wfd(f64(defaultNaNF64UI)); \
	else \
	  wfd(less || isNaNF64UI(f64(f2).v) ? f1 : f2); \
      }

#define fmax_d_body() \
      { \
	bool greater = f64_lt_quiet(f64(f2), f64(f1)) || (f64_eq(f64(f2), f64(f1)) && (f64(f2).v & F64_SIGN)); \
	if (isNaNF64UI(f64(f1).v) && isNaNF64UI(f64(f2).v)) \
	  wfd(f64(defaultNaNF64UI)); \
	else \
	  wfd(greater || isNaNF64UI(f64(f2).v) ? f1 : f2); \
      }

    case Op_flw:  wfd(f32(MMU.load_uint32(r1+imm)));  pc+=4; break;
    case Op_fsw:  MMU.store_uint32(r1+imm, f2.v[0]);  pc+=4; break;
      
    case Op_fmadd_s:  srm; wfd(f32_mulAdd(f32(f1),                 f32(f2),     f32(f3)            )); sfx; pc+=4; break;
    case Op_fmsub_s:  srm; wfd(f32_mulAdd(f32(f1),                 f32(f2), f32(f32(f3).v^F32_SIGN))); sfx; pc+=4; break;
    case Op_fnmsub_s: srm; wfd(f32_mulAdd(f32(f32(f1).v^F32_SIGN), f32(f2),     f32(f3)            )); sfx; pc+=4; break;
    case Op_fnmadd_s: srm; wfd(f32_mulAdd(f32(f32(f1).v^F32_SIGN), f32(f2), f32(f32(f3).v^F32_SIGN))); sfx; pc+=4; break;
      
    case Op_fadd_s:  srm; wfd(f32_add(f32(f1), f32(f2))); sfx; pc+=4; break;
    case Op_fsub_s:  srm; wfd(f32_sub(f32(f1), f32(f2))); sfx; pc+=4; break;
    case Op_fmul_s:  srm; wfd(f32_mul(f32(f1), f32(f2))); sfx; pc+=4; break;
    case Op_fdiv_s:  srm; wfd(f32_div(f32(f1), f32(f2))); sfx; pc+=4; break;
    case Op_fsqrt_s: srm; wfd(f32_sqrt(f32(f1)));         sfx; pc+=4; break;

    case Op_fsgnj_s:      wfd(fsgnj32(f1, f2, false, false));  pc+=4; break;
    case Op_fsgnjn_s:     wfd(fsgnj32(f1, f2, true,  false));  pc+=4; break;
    case Op_fsgnjx_s:     wfd(fsgnj32(f1, f2, false,  true));  pc+=4; break;
    case Op_fmin_s:  fmin_s_body(); pc+=4; break;
    case Op_fmax_s:  fmax_s_body(); pc+=4; break;

    case Op_fcvt_w_s:  srm;  wrd(sext32(f32_to_i32( f32(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fcvt_wu_s: srm;  wrd(sext32(f32_to_ui32(f32(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fmv_x_w:         wrd(sext32(f1.v[0]));                             pc+= 4; break;

    case Op_feq_s:           wrd(f32_eq(f32(f1), f32(f2)));                  pc+= 4; break;
    case Op_flt_s:           wrd(f32_lt(f32(f1), f32(f2)));                  pc+= 4; break;
    case Op_fle_s:           wrd(f32_lt(f32(f1), f32(f2)));                  pc+= 4; break;
    case Op_fclass_s:        wrd(f32_classify(f32(f1)));                       pc+= 4; break;
				      
    case Op_fcvt_s_w:  srm;  wfd(i32_to_f32( ( int32_t)r1));              sfx; pc+= 4; break;
    case Op_fcvt_s_wu: srm;  wfd(ui32_to_f32((uint32_t)r1));              sfx; pc+= 4; break;
    case Op_fmv_w_x:         wfd(f32(r1));                                     pc+= 4; break;

    case Op_fcvt_l_s:  srm;  wrd(sext32(f32_to_i64( f32(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fcvt_lu_s: srm;  wrd(sext32(f32_to_ui64(f32(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fcvt_s_l:  srm;  wfd(i64_to_f32( ( int32_t)r1));              sfx; pc+= 4; break;
    case Op_fcvt_s_lu: srm;  wfd(ui64_to_f32((uint32_t)r1));              sfx; pc+= 4; break;

    case Op_fld:  wfd(f64(MMU.load_uint64(r1+imm)));  pc+=4; break;
    case Op_fsd:  MMU.store_uint64(r1+imm, f2.v[0]);  pc+=4; break;

    case Op_fmadd_d:  srm; wfd(f64_mulAdd(f64(f1),                 f64(f2),     f64(f3)            )); sfx; pc+=4; break;
    case Op_fmsub_d:  srm; wfd(f64_mulAdd(f64(f1),                 f64(f2), f64(f64(f3).v^F64_SIGN))); sfx; pc+=4; break;
    case Op_fnmsub_d: srm; wfd(f64_mulAdd(f64(f64(f1).v^F64_SIGN), f64(f2),     f64(f3)            )); sfx; pc+=4; break;
    case Op_fnmadd_d: srm; wfd(f64_mulAdd(f64(f64(f1).v^F64_SIGN), f64(f2), f64(f64(f3).v^F64_SIGN))); sfx; pc+=4; break;

    case Op_fadd_d:  srm; wfd(f64_add(f64(f1), f64(f2))); sfx; pc+=4; break;
    case Op_fsub_d:  srm; wfd(f64_sub(f64(f1), f64(f2))); sfx; pc+=4; break;
    case Op_fmul_d:  srm; wfd(f64_mul(f64(f1), f64(f2))); sfx; pc+=4; break;
    case Op_fdiv_d:  srm; wfd(f64_div(f64(f1), f64(f2))); sfx; pc+=4; break;
    case Op_fsqrt_d: srm; wfd(f64_sqrt(f64(f1)));         sfx; pc+=4; break;

    case Op_fsgnj_d:      wfd(fsgnj64(f1, f2, false, false));  pc+=4; break;
    case Op_fsgnjn_d:     wfd(fsgnj64(f1, f2, true,  false));  pc+=4; break;
    case Op_fsgnjx_d:     wfd(fsgnj64(f1, f2, false,  true));  pc+=4; break;
    case Op_fmin_d:  fmin_d_body(); pc+=4; break;
    case Op_fmax_d:  fmax_d_body(); pc+=4; break;

    case Op_fcvt_s_d:  srm;  wfd(f64_to_f32(f64(f1)));                   sfx; pc+= 4; break;
    case Op_fcvt_d_s:  srm;  wfd(f32_to_f64(f32(f1)));                   sfx; pc+= 4; break;

    case Op_feq_d:           wrd(f64_eq(f64(f1), f64(f2)));                  pc+= 4; break;
    case Op_flt_d:           wrd(f64_lt(f64(f1), f64(f2)));                  pc+= 4; break;
    case Op_fle_d:           wrd(f64_lt(f64(f1), f64(f2)));                  pc+= 4; break;
    case Op_fclass_d:        wrd(f64_classify(f64(f1)));                       pc+= 4; break;

    case Op_fcvt_w_d:  srm;  wrd(sext32(f64_to_i32( f64(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fcvt_wu_d: srm;  wrd(sext32(f64_to_ui32(f64(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fcvt_d_w:  srm;  wfd(i32_to_f64( ( int32_t)r1));              sfx; pc+= 4; break;
    case Op_fcvt_d_wu: srm;  wfd(ui32_to_f64((uint32_t)r1));              sfx; pc+= 4; break;

    case Op_fcvt_l_d:  srm;  wrd(sext32(f64_to_i64( f64(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fcvt_lu_d: srm;  wrd(sext32(f64_to_ui64(f64(f1), RM, true))); sfx; pc+= 4; break;
    case Op_fmv_x_d:         wrd(f1.v[0]);                                     pc+= 4; break;
    case Op_fcvt_d_l:  srm;  wfd(i64_to_f64( ( int64_t)r1));              sfx; pc+= 4; break;
    case Op_fcvt_d_lu: srm;  wfd(ui64_to_f64((uint64_t)r1));              sfx; pc+= 4; break;
    case Op_fmv_d_x:         wfd(f64(r1));                                     pc+= 4; break;


    case Op_lr_w: { int32_t res=MMU.load_int32(r1, true); MMU.acquire_load_reservation(r1);                                      wrd( res); } pc+=4; break;
    case Op_sc_w: { bool yes=MMU.check_load_reservation(r1, 4); if (yes) MMU.store_uint32(r1, r2); MMU.yield_load_reservation(); wrd(!yes); } pc+=4; break;
    case Op_amoswap_w:	wrd(sext32(MMU.amo_uint32(r1, [&](uint32_t lhs) { return r2; })));       pc+=4; break;
    case Op_amoadd_w:	wrd(sext32(MMU.amo_uint32(r1, [&]( int32_t lhs) { return lhs + r2; }))); pc+=4; break;
    case Op_amoxor_w:	wrd(sext32(MMU.amo_uint32(r1, [&](uint32_t lhs) { return lhs ^ r2; }))); pc+=4; break;
    case Op_amoand_w:	wrd(sext32(MMU.amo_uint32(r1, [&](uint32_t lhs) { return lhs & r2; }))); pc+=4; break;
    case Op_amoor_w:	wrd(sext32(MMU.amo_uint32(r1, [&](uint32_t lhs) { return lhs | r2; }))); pc+=4; break;
    case Op_amomin_w:	wrd(sext32(MMU.amo_uint32(r1, [&]( int32_t lhs) { return std::min(lhs,  int32_t(r2)); }))); pc+=4; break;
    case Op_amomax_w:	wrd(sext32(MMU.amo_uint32(r1, [&]( int32_t lhs) { return std::max(lhs,  int32_t(r2)); }))); pc+=4; break;
    case Op_amominu_w:	wrd(sext32(MMU.amo_uint32(r1, [&](uint32_t lhs) { return std::min(lhs, uint32_t(r2)); }))); pc+=4; break;
    case Op_amomaxu_w:	wrd(sext32(MMU.amo_uint32(r1, [&](uint32_t lhs) { return std::max(lhs, uint32_t(r2)); }))); pc+=4; break;

    case Op_lr_d: { int64_t res=MMU.load_int64(r1, true); MMU.acquire_load_reservation(r1);                                      wrd( res); } pc+=4; break;
    case Op_sc_d: { bool yes=MMU.check_load_reservation(r1, 4); if (yes) MMU.store_uint64(r1, r2); MMU.yield_load_reservation(); wrd(!yes); } pc+=4; break;
    case Op_amoswap_d:	wrd(MMU.amo_uint64(r1, [&](uint64_t lhs) { return r2; }));       pc+=4; break;
    case Op_amoadd_d:	wrd(MMU.amo_uint64(r1, [&]( int64_t lhs) { return lhs + r2; })); pc+=4; break;
    case Op_amoxor_d:	wrd(MMU.amo_uint64(r1, [&](uint64_t lhs) { return lhs ^ r2; })); pc+=4; break;
    case Op_amoand_d:	wrd(MMU.amo_uint64(r1, [&](uint64_t lhs) { return lhs & r2; })); pc+=4; break;
    case Op_amoor_d:	wrd(MMU.amo_uint64(r1, [&](uint64_t lhs) { return lhs | r2; })); pc+=4; break;
    case Op_amomin_d:	wrd(MMU.amo_uint64(r1, [&]( int64_t lhs) { return std::min(lhs,  int64_t(r2)); })); pc+=4; break;
    case Op_amomax_d:	wrd(MMU.amo_uint64(r1, [&]( int64_t lhs) { return std::max(lhs,  int64_t(r2)); })); pc+=4; break;
    case Op_amominu_d:	wrd(MMU.amo_uint64(r1, [&](uint64_t lhs) { return std::min(lhs, uint64_t(r2)); })); pc+=4; break;
    case Op_amomaxu_d:	wrd(MMU.amo_uint64(r1, [&](uint64_t lhs) { return std::max(lhs, uint64_t(r2)); })); pc+=4; break;

    case Op_ILLEGAL:  die("Op_ILLEGAL opcode");
    case Op_UNKNOWN:  die("Op_UNKNOWN opcode");
      
#if 0
    default:
      try {
	pc = golden[i.opcode()](pc, *mmu(), spike());
      } catch (trap_user_ecall& e) {
	write_pc(pc);
	proxy_ecall(insns);
	pc += 4;
      } catch (trap_breakpoint& e) {
	write_pc(pc);
	incr_count(insns);
	return true;
      }
#endif
    } // switch (i.opcode())
    xrf[0] = 0;
#ifdef DEBUG
    i = code.at(oldpc);
    int rn = i.rd()==NOREG ? i.rs2() : i.rd();
    debug.addval(i.rd(), read_reg(rn));
#endif
  } while (++insns < how_many);
  write_pc(pc);
  incr_count(insns);
  return false;
}

