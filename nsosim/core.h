/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

enum Reason_t { Idle, Ready, Regs_busy, Bus_busy,
		No_freereg, IQ_full, Stb_full, St_unknown_addr,
		Br_regs_busy, Br_bus_busy, Flush_wait, Br_jumped,
		Port_busy, Stb_checker_busy, Dependency_detected,
		Number_of_Reasons
};
extern const char* reason_name[];



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

  Reason_t not_dispatch[cycle_history];	// reason did not dispatch this cycle
  Reason_t not_execute [cycle_history];	// reason did not execute this cycle
  long long dispatch_stalls[Number_of_Reasons];
  long long execute_stalls[Number_of_Reasons];

  // Current instruction waiting for dispatch
  Header_t* bb;			// current basic blocka
  Insn_t* i;			// current decoded instruction
  Addr_t pc;			// at this address

  bool mem_checker_used;	// in this cycle
  History_t* immediate_h;	// instruction that executed immediately
  bool inhibit_dispatch;	// due to taken branch
  
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
  
  friend void clock_memory_system(Core_t* cpu);
  friend void display_history(WINDOW* w, int y, int x, Core_t* c, int lines);
  void display_stall_reasons(WINDOW* w, int y, int x);
  friend void interactive(Core_t* cpu);

  Reason_t ready_to_dispatch(Insn_t ir);
  Reason_t ready_to_execute(History_t* h);

  void writeback_stage();
  Addr_t execute_instruction(History_t* h);

};

long ooo_riscv_syscall(hart_t* h, long a0);
int clone_proxy(hart_t* h);

