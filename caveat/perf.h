

struct perf_header_t {		// performance segment header
  long size;			// of shared memory segment in bytes
  long parcels;			// length of text segment (2B parcels)
  long base;			// address of code segment
  long cores;			// number of simulated cores
  long max_cores;		// maximum number of cores allocated
  long arrays[0];		// beginning of dynamic arrays
};

struct count_t {			// per-instruction counters
  long executed;		// number of times executed
  long cycles;			// total number of cycles
};

class perf_t {			// pointers into shared memory structure
  perf_header_t* h;		// shared segment
  count_t** _count;		// _count[k] for core k
  long** _imiss;		// _imiss[k][n] I-cache misss
  long** _dmiss;		//  by instruction n
  const char* name;		// of shared segment
  int fd;			// associated file descriptor
  void common_init();		// pointers into shared segment
public:
  perf_t(long base, long bound, long n, const char* shm_name); // writer
  perf_t(const char* shm_name);			// reader
  perf_t() {}					// for static variable
  ~perf_t();					// unmap shared segment
  long count( long pc, long k) { return _count[k][(pc-h->base)/2].executed; }
  long cycles(long pc, long k) { return _count[k][(pc-h->base)/2].cycles; }
  long imiss( long pc, long k) { return _imiss[k][(pc-h->base)/2]; }
  long dmiss( long pc, long k) { return _dmiss[k][(pc-h->base)/2]; }
};
