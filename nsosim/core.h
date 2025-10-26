/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

union simreg_t {
  reg_t x;			// for integer
  freg_t f;			// float and double
  uintptr_t a;			// store buffer entry
};

// replicates processor state in uspike but with more physical registers
struct simulator_state_t {
  simreg_t reg[regfile_size];
  unsigned fflags;
  unsigned frm;
};

extern long long mismatches;	// count number of mismatches

class Core_t : public hart_t {
  long long _insns;		// count number of instructions executed
  long long _inflight;		// number of instructions in flight
public:
  simulator_state_t s;		// replaces uspike state
  Remapping_Regfile_t regs;	// physical register file
private:
  
  Port_t port;			// memory port

  // issue queue
  History_t* queue[issue_queue_length];
  int last;			// queue depth

  // Structures for visual debugging
  History_t rob[dispatch_history];     // phantom reorder buffer
      
  uint16_t cycle_flags[cycle_history]; // what happend this cycle
#define FLAG_busy	0x001	// instruction has busy registers
#define FLAG_qfull	0x002	// issue queue is full
#define FLAG_stuaddr	0x004	// store address not yet available
#define FLAG_stbfull	0x008	// store buffer is full
#define FLAG_serialize	0x010	// waiting for pipeline to flush
#define FLAG_nofree	0x020	// register free list is empty
#define FLAG_stbhit	0x040	// load store buffer check hit
#define FLAG_endmem	0x100	// retired a memory operation
#define FLAG_noport	0x200	// memory port busy
#define FLAG_stbchk	0x400	// store buffer checker busy
#define FLAG_regbus	0x1000	// register file write bus busy
  

  // Current instruction waiting for dispatch
  Header_t* bb;			// current basic blocka
  Insn_t* i;			// current decoded instruction
  Addr_t pc;			// at this address

  friend long ooo_riscv_syscall(hart_t* h, long a0);
  friend int clone_proxy(hart_t* h);

public:
  void reset();
  Core_t(hart_t* from) :hart_t(from) { reset(); }
  Core_t(int argc, const char* argv[], const char* envp[]) :hart_t(argc, argv, envp) { reset(); }

  long long inflight() { return _inflight; }
  History_t* nextrob() { return &rob[_insns % dispatch_history]; }
  //History_t* nextrob(int k =0) { return &rob[(_insns+k+dispatch_history) % dispatch_history]; }

  Addr_t mem_addr(Insn_t ir) {
    assert(attributes[ir.opcode()] & (ATTR_ld|ATTR_st));
    Addr_t a = s.reg[ir.rs1()].x;
    if (! (attributes[ir.opcode()] & ATTR_amo))
      a += ir.immed();
    a &= ~0x7L;
    return a;
  }

  void clock_port();
  bool clock_pipeline();
  void show_reg(WINDOW* w, Reg_t n, char sep, int ref);
  
  void rename_input_regs(Insn_t& ir);
  //void rename_output_reg(Insn_t& ir);
  bool ready_insn(Insn_t ir);
  Reg_t check_store_buffer(uintptr_t addr, int k =0); // from k-1 position
  
  uintptr_t get_state();
  void put_state(uintptr_t pc);
  uintptr_t get_rd_from_spike(Reg_t n);
  uintptr_t get_pc_from_spike();

  Addr_t perform(Insn_t* i, Addr_t pc, History_t* h);

  static Core_t* list() { return (Core_t*)hart_t::list(); }
  Core_t* next() { return (Core_t*)hart_t::next(); }

  long long insns() { return _insns; }
  void test_run();
  
  friend void clock_memory_system(Core_t* cpu);
  friend void display_history(WINDOW* w, int y, int x, Core_t* c, int lines);
  friend void interactive(Core_t* cpu);

#ifdef VERIFY
  template<typename op>	int32_t amo_int32(uintptr_t a, op f, uintptr_t*& ap) {
    return rob[cycle % dispatch_history].expected_rd;
  }
  template<typename op>	int64_t amo_int64(uintptr_t a, op f, uintptr_t*& ap) {
    return rob[cycle % dispatch_history].expected_rd;
  }
#endif
};

long ooo_riscv_syscall(hart_t* h, long a0);
int clone_proxy(hart_t* h);
