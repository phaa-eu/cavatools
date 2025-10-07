
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

  //  Header_t** target = &mismatch;
  //  Header_t** target;
  //  Header_t* bb;			// current basic block
  

  void initialize() { };

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
  friend void my_riscv_syscall(hart_t* h);
};
