/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

const int issue_queue_length = 16;
const int store_buffer_length = 8;

const int cycle_history = 256;
const int dispatch_history = 128;

// timing wheel for simulating pipelines
const int num_write_ports = 1;
const int max_latency = 32;	// for timing wheel

const int memory_channels = 1;
const int memory_banks = 8;

extern thread_local long unsigned cycle;	// count number of processor cycles


// Unified physical register file + store buffer.
//   First part holds physical registers with a free list.
//   Second part holds store buffer entries.
//
const int max_phy_regs = 128;
const int regfile_size = max_phy_regs + store_buffer_length;

inline bool is_store_buffer(uint8_t r) {
  return max_phy_regs<=r && r<max_phy_regs+store_buffer_length;
}

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

// Two structures for visual debugging:
//   1.  Per cycle flags indicating what happend on that cycle
//   2.  Per instruction dispatched to show renaming, etc.
//       Like a phantom reorder buffer.

#define FLAG_busy	0x001	// instruction has busy registers
#define FLAG_qfull	0x002	// issue queue is full
#define FLAG_stuaddr	0x004	// store address not yet available
#define FLAG_stbfull	0x008	// store buffer is full
#define FLAG_serialize	0x010	// waiting for pipeline to flush
#define FLAG_nofree	0x020	// register free list is empty

// Phantom reorder buffer for visual debugging
struct History_t {
  Insn_t* ref;			// original instruction
  uintptr_t pc;			// at this PC
  Insn_t insn;			// with renamed registers
  unsigned long cycle;		// dispatch time
  enum { STATUS_retired, STATUS_execute, STATUS_queue, STATUS_dispatch } status;
  void display(WINDOW* w, bool busy[], unsigned uses[]);
private:
  void show_opcode(WINDOW* w, bool executing);
  void show_reg(WINDOW* w, char sep, int orig, int phys, bool busy[], unsigned uses[]);
};


// Memory bank descriptor

struct membank_t {
  uintptr_t addr;		// working on this address
  long unsigned finish;		// cycle number
  //History_t* h;			// for accessing flags
  uint8_t rd;			// destination register or associated store buffer
  bool active() { return rd != NOREG; }
  membank_t()
  { rd = NOREG; }
};


// Memory system modelled outside of processor core

extern thread_local long unsigned cycle;	// count number of processor cycles
extern thread_local membank_t memory[memory_channels][memory_banks];

// one simulation thread
class core_t : public hart_t {
public:
  simulator_state_t s;		// replaces uspike state
  long unsigned insns;		// count number of instructions executed
  long outstanding;		// number of instructions in flight

  Header_t* bb;			// current basic blocka
  Insn_t* i;			// current decoded instruction
  uintptr_t pc;			// at this address

  // renaming register file
  uint8_t regmap[256];		// architectural to physical
  
  bool busy[regfile_size];	// register waiting to be filled
  unsigned uses[regfile_size];	// number of readers
  
  uint8_t freelist[max_phy_regs];
  int numfree;			// maintained as stack
  bool no_free_reg(Insn_t ir) { return !ir.rd() || ir.rd()==NOREG || numfree==max_phy_regs-64; };

  // store buffer (within physical register file)
  int nextstb;	       // next store position in circular store buffer
  int stbuf(int k) { assert(0<=k && k<store_buffer_length);
    return (nextstb-k+store_buffer_length)%store_buffer_length + max_phy_regs; }
  //bool stbuf_full() { return uses[stbuf(0)] > 0; }
  bool stbuf_full() { return busy[stbuf(0)]; }

  // issue queue
  History_t* queue[issue_queue_length];
  int last;			// queue depth

  // pipeline model for instruction finish time
  History_t* wheel[num_write_ports][max_latency+1];
  int index(unsigned k) { assert(k<=max_latency); return (cycle+k)%(max_latency+1); }


  // what happened on this cycle
  uint16_t cycle_flags[cycle_history];

  // phantom reorder buffer for visual debugging
  // rob indexed by [executed() % history_depth]
  History_t rob[dispatch_history];
  
  void rename_input_regs(Insn_t& ir);
  void rename_output_reg(Insn_t& ir);
  bool ready_insn(Insn_t ir);
  void acquire_reg(uint8_t r);
  void release_reg(uint8_t r);
  
  uintptr_t get_state();
  void put_state(uintptr_t pc);

  uintptr_t perform(Insn_t* i, uintptr_t pc);
  
public:
  core_t(hart_t* from) :hart_t(from) { }
  core_t(int argc, const char* argv[], const char* envp[]) :hart_t(argc, argv, envp) { }

  long unsigned cpu_cycles() { return cycle; }
  
  friend long ooo_riscv_syscall(hart_t* h, long a0);
  friend int clone_proxy(hart_t* h);
  friend void simulator(hart_t* h);

  void init_simulator();
  void simulate_cycle();
  void fini_simulator();

  static core_t* list() { return (core_t*)hart_t::list(); }
  core_t* next() { return (core_t*)hart_t::next(); }

  void display_history(WINDOW*w, int y, int x, int lines);
  void interactive();
  void run_fast();
};

long ooo_riscv_syscall(hart_t* h, long a0);
int clone_proxy(hart_t* h);

extern uint8_t latency[];
