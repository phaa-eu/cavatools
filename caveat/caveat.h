/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifndef CAVEAT_H
#define CAVEAT_H
  
typedef long Addr_t;
#define GEN_SEGV  (*((char*)0) = 1)

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

/*
  Simulation configuration.
*/
struct conf_t {
  long simulate;		/* do performance simulation */
  const char* func;		/* name of function to analyze */
  Addr_t breakpoint;		/* entrypoint of func */
  long after;			/* countdown, negative=start pipeline simulation */
  long every;			/* simulate every n-th calls */
  long ecalls;			/* log system calls */
  long visible;			/* show each instruction execution */
  
  long report;			/* interval, in millions of instructions */
  long quiet;			/* no progress report */
  const char* perf;		/* name of shared segment with counters */
  long cores;			/* number of cores in simulation */
  long mhz;			/* pretend clock MHz */
  long branch;			/* branch delay */
  long load;			/* load latency */
  long fma;			/* fused multiply add pipeline depth */
  long ipenalty;		/* IC cache miss penalty */
  long iways;			/* IC set associativity */
  long iline;			/* IC line size */
  long irows;			/* IC number of sets */
  long dpenalty;		/* DC cache miss penalty */
  long dways;			/* DC set associativity */
  long dline;			/* DC line size */
  long drows;			/* DC number of sets */
};

extern struct conf_t conf;

extern const char* ascii_color[];
#define color(n) ascii_color[(n)%8]
#define nocolor "\e[39m"

#endif
