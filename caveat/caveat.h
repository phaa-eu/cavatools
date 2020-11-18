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
#define tr_number(tr)  ( (uint64_t)(tr)                        >> 54)

/*
  The opcodes are defined here.  You may add additional encodings.
*/

#define tr_eof		0b0000000L		/* M-format */
#define tr_icount	0b0000001L 		/* M-format */
#define tr_any		0b0000010L		/* P-format */
#define tr_fence	0b0000011L		/* P-format */
#define tr_ecall	0b0000100L		/* M-format */
#define tr_csr		0b0000101L		/* M-format */
#define tr_issue	0b0000110L		/* P-format */

#define is_goto(tr)   ((0b1110100L & tr_code(tr)) == \
		       (0b0010100L))		/* P-format (all) */
#define tr_start	0b0010000L
#define tr_jump		0b0010100L
#define tr_branch	0b0010101L
#define tr_call		0b0010110L
#define tr_return	0b0010111L

/*
  All memory reference opcodes have MSB=1
  There is a consistent bit for read/write
  Divided into is_ldst() load/store group, with tr_size() 1, 2, 4 8 bytes
  and cache line is_getput() group, for tr_level() 0, 1, 2, 3 caches
*/
#define is_mem(tr)    ((0b1000000L & tr_code(tr)) == \
		       (0b1000000L))		/* M-format (all) */
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



/*
  Macros to create trace records.
*/
#define trM(code, value)       ( ((uint64_t)( value)<< 7)                                             | ((uint64_t)(code)&0x7fL) )
#define trP(code, number, pc)  ( ((uint64_t)(number)<<54) | ((uint64_t)(pc)<<6) & 0x003fffffffffff80L | ((uint64_t)(code)&0x7fL) )



#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }


static inline uint64_t tr_print(uint64_t tr)
{
  if (is_mem(tr))
    fprintf(stderr, "MemOp: code=%02lx, w=%d, sz=%ldB, addr=0x%lx\n", tr_code(tr), is_write(tr), tr_size(tr), tr_value(tr));
  else if (is_goto(tr) && tr!=tr_start)
    fprintf(stderr, "GotoOp: code=%02lx, number=%ld, pc=0x%lx\n", tr_code(tr), tr_number(tr), tr_pc(tr));
  else if (tr_code(tr) == tr_any)
    fprintf(stderr, "tr_any: number=%ld, pc should be zero 0x%lx\n", tr_number(tr), tr_pc(tr));
  else
    fprintf(stderr, "OtherOp: code=%02lx, number=%ld, pc=0x%lx\n", tr_code(tr), tr_number(tr), tr_pc(tr));
  return tr;
}


#define trace_init(tb, shm_path, consumer)  fifo_init(tb, shm_path, consumer)
#define trace_fini(tb)  { fifo_put(tb, trM(tr_eof, 0)); fifo_fini(tb); }



struct options_t {
  const char* name;
  union {
    const char** v;
    int* f;
  };
};

int parse_options( struct options_t opt[], const char** argv );



#endif
