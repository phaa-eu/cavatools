/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

const int max_phy_regs = 256;
const int num_write_ports = 1;

const int max_latency = 32;
const int issue_queue_length = 16;

const int history_depth = 128;

// unified physical register file
union simreg_t {
  reg_t x;			// for integer
  freg_t f;			// float and double
};

// replicates processor state in uspike
struct simulator_state_t {
  simreg_t reg[max_phy_regs];	// but with more physical registers
  unsigned fflags;
  unsigned frm;
};

// event flags for display
#define FLAG_busy	0x01	// instruction has busy registers
#define FLAG_qfull	0x02	// issue queue is full
#define FLAG_jump	0x04	// jump instruction cannot be deferred
#define FLAG_store	0x08	// store address not yet available
#define FLAG_serialize	0x10	// waiting for pipeline to flush
#define FLAG_delayed	0x20	// executing instruction from queue
#define FLAG_queue	0x40	// deferring instruction into queue
#define FLAG_free	0x80	// register free list is empty
#define FLAG_stall	0x100	// instruction must wait
#define FLAG_retire	0x200	// instruction has retired

// phantom reorder buffer for visual debugging
struct History_t {
  Insn_t insn;
  uintptr_t pc;
  Insn_t* ref;
  unsigned long cycle;
  unsigned flags;
};


// one simulation thread
class core_t : public hart_t {
  simulator_state_t s;		// replaces uspike state
  long unsigned cycle;		// count number of processor cycles
  long unsigned insns;		// count number of instructions executed
  long outstanding;		// number of instructions in flight

  Header_t* bb;			// current basic block
  Insn_t* i;			// current decoded instruction
  uintptr_t pc;			// at this address

  // renaming register file
  uint8_t regmap[256];		// architectural to physical
  bool busy[max_phy_regs];	// register waiting to be filled
  unsigned uses[max_phy_regs];	// number of readers
  uint8_t freelist[max_phy_regs-64];
  int numfree;			// maintained as stack

  // phantom reorder buffer for visual debugging
  // rob indexed by [executed() % history_depth]
  History_t rob[history_depth];

  // issue queue
  History_t* queue[issue_queue_length];
  int last;			// queue depth

  // pipeline for time when instruction finishes
  History_t* wheel[num_write_ports][max_latency+1];
  int index(unsigned k) { assert(k<=max_latency); return (cycle+k)%(max_latency+1); }

  bool no_free_reg(Insn_t ir) { return !ir.rd() || ir.rd()==NOREG || numfree==max_phy_regs-64; };
  void rename_input_regs(Insn_t& ir);
  bool ready_insn(Insn_t ir);
  void commit_insn(Insn_t& ir);
  void acquire_reg(uint8_t r);
  void release_reg(uint8_t r);
  
  uintptr_t get_state();
  void put_state(uintptr_t pc);

  void try_issue_from_queue();
  uintptr_t perform(Insn_t* i, uintptr_t pc);

  void initialize() { memset(&s, 0, sizeof s); }
public:
  core_t(hart_t* from) :hart_t(from) { initialize(); }
  core_t(int argc, const char* argv[], const char* envp[]) :hart_t(argc, argv, envp) { initialize(); }

  long unsigned cpu_cycles() { return cycle; }
  
  friend long ooo_riscv_syscall(hart_t* h, long a0);
  friend int clone_proxy(hart_t* h);
  friend void simulator(hart_t* h);

  void init_simulator();
  void simulate_cycle();
  void fini_simulator();

  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  void display_history();
};

long ooo_riscv_syscall(hart_t* h, long a0);
int clone_proxy(hart_t* h);

extern uint8_t latency[];
