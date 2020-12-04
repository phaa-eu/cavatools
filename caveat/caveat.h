/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifndef CAVEAT_H
#define CAVEAT_H

/*
  Caveat trace records are a kind of instruction set with a 7-bit 'opcode'.
  There are two main instruction formats:
    M (memory reference) has signed 54-bit value, usually a virtual address
    P (program counter) has 47-bit PC (RISC-V instructions minimum 16-bytes)
        plus a 10 bit number field, usually byte count since last P record.

   6         5         4         3         2         1         0  bit pos
3210987654321098765432109876543210987654321098765432109876543210  --------
v.......v.......v.......v.......v.......v.......v.......vccccccc  M format
nnnnnnnnnn......p.......p.......p.......p.......p.......pccccccc  P format

*/ 
#define tr_max_number	(1UL<<10)		/* P-format number field  */
#define tr_memq_len     (tr_max_number/2)	/* number of instructions */

#define tr_code(tr)     ((uint64_t)(tr) & 0x000000000000007fL)
#define tr_value(tr)   ( ( int64_t)(tr)                        >>  7)
#define tr_pc(tr)      (((uint64_t)(tr) & 0x003fffffffffff80L) >>  6)
#define tr_delta(tr)   ( (uint64_t)(tr)                        >> 54)
#define tr_number(tr)  (((uint64_t)(tr) & 0x003fffffffffff80L) >>  7)

/*  Trace file is broken into frames.  Each frame comes from a single HART.
    Frame records are P-format with number=hart# and pc=begining value. */
#define is_frame(tr)  ((0b1110000L & tr_code(tr)) == \
		       (0b0000000L))
#define tr_eof		0b0000000L
#define tr_has_pc	0b0000001L /* taken branch targets */
#define tr_has_mem	0b0000010L /* load/store addresses */
#define tr_has_reg	0b0000100L /* register update values */
#define tr_has_timing	0b0001000L /* pipeline timing information */

/*  The main trace file record types are memory and basic block records. */
#define is_mem(tr)    ((0b1000000L & tr_code(tr)) == \
		       (0b1000000L))		/* M-format */
#define is_bbk(tr)    ((0b1100000L & tr_code(tr)) == \
		       (0b0100000L))		/* P-format */

/*  Basic block records are P-format, describing a series sequential instructions.
    The number tr_field gives the length of the instruction block in bytes.
    If the block ends in a taken branch, the tr_pc field gives the target address.
    Otherwise the tr_pc field is the next sequential basic block beginning address. */

#define tr_jump		0b0100000L
#define tr_branch	0b0100001L
#define tr_call		0b0100010L
#define tr_return	0b0100011L
#define is_goto(tr)   ((0b1111000L & tr_code(tr)) == \
		       (0b0100000L)) /* 4 spare opcodes */
/* Below are basic block records not ending in taken branch. */
#define tr_any		0b0101000L		/* P-format */
#define tr_fence	0b0101001L		/* P-format */
#define tr_ecall	0b0101010L		/* M-format */
#define tr_csr		0b0101011L		/* M-format */

/*
  All memory records are M-format, have consistent bit for read/write, are
  divided into is_ldst() load/store group with tr_size() 1, 2, 4 8 bytes,
  and cache line is_getput() group for tr_level() 0, 1, 2, 3 caches.
*/
#define is_write(tr)  ((0b1000100L & tr_code(tr)) == \
		       (0b1000100L))
#define is_ldst(tr)   ((0b1100000L & tr_code(tr)) == \
		       (0b1000000L))
#define is_getput(tr) ((0b1100000L & tr_code(tr)) == \
		       (0b1100000L))

#define is_amo(tr)    ((0b1111100L & tr_code(tr)) == \
		       (0b1000000L))
#define tr_amo4		0b1000110L
#define tr_amo8		0b1000111L

#define is_lrsc(tr)   ((0b1111100L & tr_code(tr)) == \
		       (0b1001000L))
#define tr_lr4		0b1001010L
#define tr_lr8		0b1001011L
#define tr_sc4		0b1001110L
#define tr_sc8		0b1001111L

#define is_load(tr)   ((0b1111100L & tr_code(tr)) == \
		       (0b1010000L))
#define tr_read1	0b1010000L
#define tr_read2	0b1010001L
#define tr_read4	0b1010010L
#define tr_read8	0b1010011L
#define is_store(tr)  ((0b1111100L & tr_code(tr)) == \
		       (0b1010100L))
#define tr_write1	0b1010100L
#define tr_write2	0b1010101L
#define tr_write4	0b1010110L
#define tr_write8	0b1010111L
#define tr_size(tr)            (1L<<(tr_code(tr)&0x3L))

#define tr_d1get	0b1100001L	/* L1 data cache load from L2 */
#define tr_d1put	0b1100101L	/* L1 data cache write back to L2 */
#define tr_d2get	0b1100010L	/* L2 data cache load from L3 */
#define tr_d2put	0b1100110L	/* L2 data cache write back to L3 */
#define is_dcache(tr) ((0b1111000L & tr_code(tr)) == \
		       (0b1100000L))
#define tr_i0get	0b1110000L	/* instruction buffer fetchfrom L1 */
#define tr_i1get	0b1110001L	/* L1 instruction cache load from L2 */
#define tr_i2get	0b1110010L	/* L2 instruction cache load from L3 */
#define is_icache(tr) ((0b1111000L & tr_code(tr)) == \
		       (0b1110000L))
#define tr_clevel(tr)          (tr_code(tr)&0x3L)

/*  Out-Of-Band Records.  These records can be inserted anytime between
    mem and bbk records, but not in the register value section because
    there the 64-bit values have no opcode field.  */

/* Tracing instruction issue cycle time */
#define tr_stall	0b0010000L		/* begin stall cycle time (M-fmt) */
#define tr_issue	0b0010001L		/* issue after number cycles (P-fmt) */

/* Periodical counters to help synchronize simulator components (M-format). */ 
#define tr_icount	0b0010010L 		/* instructions executed */
#define tr_cycles	0b0010011L 		/* pipeline cycles simulated */



/*
  Macros to create trace records.
*/
#define trM(code, value)       ( ((uint64_t)( value)<< 7)                                             | ((uint64_t)(code)&0x7fL) )
#define trP(code, number, pc)  ( ((uint64_t)(number)<<54) | ((uint64_t)(pc)<<6) & 0x003fffffffffff80L | ((uint64_t)(code)&0x7fL) )



#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }


static inline uint64_t tr_print(uint64_t tr, FILE* f)
{
  if (is_mem(tr))
    fprintf(f, "MemOp: code=%02lx, w=%d, sz=%ldB, addr=0x%lx\n", tr_code(tr), is_write(tr), tr_size(tr), tr_value(tr));
  else if (is_goto(tr))
    fprintf(f, "GotoOp: code=%02lx, delta=%ld, pc=0x%lx\n", tr_code(tr), tr_delta(tr), tr_pc(tr));
  else if (is_bbk(tr))
    fprintf(f, "BbkOp: code=%02lx, delta=%ld\n", tr_code(tr), tr_delta(tr));
  else if (tr_code(tr) == tr_stall)
    fprintf(f, "Stall: number=%ld\n", tr_number(tr));
  else if (tr_code(tr) == tr_issue)
    fprintf(f, "after=%ld, pc=0x%lx\n", tr_delta(tr), tr_pc(tr));
  else
    fprintf(f, "OtherOp=%016lx, code=%02lx, delta=%ld, pc=0x%lx\n", tr, tr_code(tr), tr_delta(tr), tr_pc(tr));
  return tr;
}


struct options_t {
  const char* name;
  const char* h;
  union {
    const char** v;
    int* f;
  };
};

void help_exit();
int parse_options( struct options_t opt[], const char** argv, const char* usage );



#endif
