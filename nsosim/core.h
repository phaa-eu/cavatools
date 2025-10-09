
const int num_deferred_insns = 2;


template<typename T, int N> class queue_t {
  int f, r;
  T element[N+1];
  void P(const char* m) { return; fprintf(stderr, "queue_t %s f=%d r=%d\n", m, f, r); }
public:
  queue_t() { memset(this, 0, sizeof *this); }
  bool empty() { return f==r; }
  bool full() { return f==(r+1)%(N+1); }
  T deque() { P("D"); assert(f!=r); T e=element[f]; f=(f+1)%(N+1); return e; }
  void enque(T e) { P("E"); element[r]=e; r=(r+1)%(N+1); assert(f!=r); }
};

struct iq_elt_t {
  Insn_t o;
  Insn_t n;
  uintptr_t p;
};

template<int N> class issueq_t {
  struct iq_elt_t iq[N];
  int last;
public:
  issueq_t() { last=0; }
  bool full() { return last==N; }
  void append(iq_elt_t  e) { assert(!full()); iq[last++]=e; }
  bool issued(iq_elt_t &e) { if (last==0) return false; e=iq[--last]; return true; }
};




union simreg_t {
  reg_t x;			// for integers
  freg_t f;			// for floats and doubles
};

struct simulator_state_t {
  simreg_t reg[256];
  unsigned fflags;
  unsigned frm;
};

#undef READ_REG
#undef READ_FREG
#undef WRITE_REG
#undef WRITE_FREG

#define READ_REG(n)   s.reg[n].x
#define READ_FREG(n)  s.reg[n].f
#define WRITE_REG(n, v)   s.reg[n].x = (v)
#define WRITE_FREG(n, v)  s.reg[n].f = (v)



class core_t : public hart_t {
  simulator_state_t s;
  Insn_t* i;			// last dispatched instruction
  uintptr_t pc;			// cooresponding pc

  uint8_t regmap[256];
  int reguses[256];
  bool regbusy[256];
  queue_t<uint8_t, num_deferred_insns+1> freelist;
  issueq_t<num_deferred_insns> issueq;

  //  Header_t** target = &mismatch;
  //  Header_t** target;
  //  Header_t* bb;			// current basic block
  

  void initialize() { }

public:
  core_t(hart_t* from) :hart_t(from) { initialize(); }
  core_t(int argc, const char* argv[], const char* envp[]) :hart_t(argc, argv, envp) { initialize(); }
  
  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  uintptr_t get_state();
  void put_state();

  //Header_t* find_bb(uintptr_t pc);
  uintptr_t perform(Insn_t* i, uintptr_t pc);

  void ooo_pipeline();
  friend long my_riscv_syscall(hart_t* h, long a0);
};
