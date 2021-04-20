/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifndef CAVEAT_H
#define CAVEAT_H

/*
  Caveat trace records are 64 bits.  The fields are
     4-bit opcode, described below
     4-bit hart number when time multiplexed
     8-bit delta cycles from last event
    48-bit memory address
  Timing event has 56-bit absolute cycle number and no delta field

   6         5         4         3         2         1         0  bit pos
3210987654321098765432109876543210987654321098765432109876543210  --------
a.......a.......a.......a.......a.......a.......ddddddddhhhhcccc  Memory Event
c.......c.......c.......c.......c.......c.......c.......hhhhcccc  Timing Event

*/

enum tr_opcode
  { tr_eof,			/* end of file/fifo */
    tr_time,			/* current simulation cycle number */
    tr_fetch,			/* instruction cache read */
    tr_remove,			/* instruction cache eviction */
    tr_shared,			/* data cache read in shared mode */
    tr_exclusive,		/* data cache read in exclusive mode */
    tr_dirty,			/* data cache change clean to dirty mode */
    tr_evict,			/* data cache clean line eviction */
    tr_update,			/* data cache dirty line write-back with eviction */
    tr_clean,			/* data cache dirty line write-back without eviction */
    tr_lr,			/* load reserved */
    tr_sc,			/* store conditional */
    tr_amo,			/* atomic read-modify-write */
  };

#define tr_code(tr)   ((uint64_t)(tr) & 0x00000000000000fL)
#define tr_hart(tr)  (((uint64_t)(tr) >>  4) & 0x0fL)
#define tr_delta(tr) (((uint64_t)(tr) >>  8) & 0xffL)
#define tr_addr(tr)   (( int64_t)(tr) >> 16)
#define tr_cycle(tr)  ((uint64_t)(tr) >>  8)


/*
  Utility stuff.
*/
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
