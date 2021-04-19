/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifndef CAVEAT_H
#define CAVEAT_H

/*
  Caveat trace records are 64 bits:
    48-bit address
    11-bit hart number
     5-bit 'opcode'
  Depending on opcode the subsequent entreis may be 64-bit values

   6         5         4         3         2         1         0  bit pos
3210987654321098765432109876543210987654321098765432109876543210  --------
a.......a.......a.......a.......a.......a.......hhhhhhhhhhhccccc  Fields

*/

#define tr_code(tr)   ((uint64_t)(tr) & 0x00000000000001fL)
#define tr_hart(tr)  (((uint64_t)(tr) >>  5) & 0x7ffL)
#define tr_addr(tr)   (( int64_t)(tr) >> 16)

#define tr_eof		0b000000L /* end of file/fifo */
#define tr_trace	0b000001L /* trace record, 2 entries:  pc, register value */

#define tr_dirty	0b000011L /* first write to cache line */
#define tr_getsh	0b000100L /* get cache line for read (shared mode)*/
#define tr_getex	0b000101L /* get cache line for write (exclusive mode) */
#define tr_evict	0b000110L /* evicted clean cache line */
#define tr_update	0b000111L /* write back dirty cache line */

#define tr_ld1		0b010000L /* load  1 byte */
#define tr_ld2		0b010010L /* load  2 bytes */
#define tr_ld4		0b010100L /* load  4 bytes */
#define tr_ld8		0b010110L /* load  8 bytes */
#define tr_ld16		0b011000L /* load 16 bytes */

#define tr_st1		0b010001L /* store  1 byte */
#define tr_st2		0b010011L /* store  2 bytes */
#define tr_st4		0b010101L /* store  4 bytes */
#define tr_st8		0b010111L /* store  8 bytes */
#define tr_st16		0b011001L /* store 16 bytes */

#define tr_lr4		0b100100L /* load reserved 4 bytes */
#define tr_lr8		0b100110L /* load reserved 8 bytes */
#define tr_sc4		0b100101L /* store conditional 4 bytes */
#define tr_sc8		0b100111L /* store conditional 8 bytes */

#define tr_amo4		0b110101L /* atomic read-modify-write 4 bytes */
#define tr_amo8		0b110111L /* atomic read-modify-write 8 bytes */

#define tr_size(tr)   ((0b001110L & tr_code(tr)) >> 1)

#define is_mem(tr)    ((0b110000L & tr_code(tr)) != \
		       (0b000000L))
#define is_ldst(tr)   ((0b110000L & tr_code(tr)) == \
		       (0b010000L))
#define is_write(tr)  ((0b000001L & tr_code(tr)) == \
		       (0b000001L))
#define is_lrsc(tr)   ((0b110000L & tr_code(tr)) == \
		       (0b100000L))
#define is_amo(tr)    ((0b110000L & tr_code(tr)) == \
		       (0b110000L))


/*
  Macros to create trace records.
*/
#define trM(code, value)       ( ((uint64_t)( value)<< 7)                                             | ((uint64_t)(code)&0x7fL) )
#define trP(code, number, pc)  ( ((uint64_t)(number)<<54) | ((uint64_t)(pc)<<6) & 0x003fffffffffff80L | ((uint64_t)(code)&0x7fL) )



#define quitif(bad, fmt, ...) if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n"); exit(0); }
#define dieif(bad, fmt, ...)  if (bad) { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n\n");  abort(); }



struct options_t {
  const char* name;		/* name=type[si] or name, preceeded by - or -- */
  union {			/* pointer to option value location */
    const char** s;		/*   name=s */
    long* i;			/*   name=i */
    long* b;			/*   name (no =) */
  };
  union {			/* default value */
    const char* ds;		/*   name=s */
    long di;			/*   name=i */
    long bv;			/* value if flag given */
  };
  const char* h;		/* help string */
};

extern const struct options_t opt[];
extern const char* usage;

void help_exit();
int parse_options( const char** argv );



#endif
