/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

const int num_write_ports = 1;

const int store_buffer_depth = 8;

const int max_latency = 32;
const int issue_queue_length = 16;

const int history_depth = 128;

const int membank_number = 8;


extern thread_local long unsigned cycle;	// count number of processor cycles


// Unified physical register file + store buffer.
//   First part holds physical registers with a free list.
//   Second part holds store buffer entries.
//

const int max_phy_regs = 128;
const int store_buffer_length = 8;
const int regfile_size = max_phy_regs + store_buffer_length;

inline bool is_store_buffer(uint8_t r) { return max_phy_regs<=r && r<max_phy_regs+store_buffer_length; }

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

// event flags for display
#define FLAG_busy	0x01	// instruction has busy registers
#define FLAG_qfull	0x02	// issue queue is full
#define FLAG_staddr	0x04	// store address not yet available
#define FLAG_stbuf	0x08	// store buffer is full
#define FLAG_serialize	0x10	// waiting for pipeline to flush
#define FLAG_free	0x20	// register free list is empty
#define FLAG_decode	0x40	// instruction in decode stage
#define FLAG_execute	0x80	// instruction is executing
#define FLAG_depend	0x100	// waiting on previous store
// flags==0 means instruction has retired

// phantom reorder buffer for visual debugging
struct History_t {
  Insn_t insn;
  uintptr_t pc;
  Insn_t* ref;
  unsigned long cycle;
  unsigned flags;
  void display(bool busy[], unsigned uses[]);
private:
  void show_opcode();
  //void show_reg(char sep, int orig, int phys, bool busy[]);
  void show_reg(char sep, int orig, int phys, bool busy[], unsigned uses[]);
};


// memory bank
struct membank_t {
  long unsigned finish;
  uint8_t rd;			// destination register or associated store buffer
};


// one simulation thread
class core_t : public hart_t {
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

  // store buffer (within physical register file)
  int nextstb;			// next store position in circular store buffer
  int stbuf(unsigned k) { assert(k<store_buffer_length);
    return (nextstb+k)%store_buffer_length +max_phy_regs; }

  // phantom reorder buffer for visual debugging
  // rob indexed by [executed() % history_depth]
  History_t rob[history_depth];

  // issue queue
  History_t* queue[issue_queue_length];
  int last;			// queue depth

  // pipeline for time when instruction finishes
  History_t* wheel[num_write_ports][max_latency+1];
  int index(unsigned k) { assert(k<=max_latency); return (cycle+k)%(max_latency+1); }

  // memory banks
  membank_t memory[membank_number];
  

  bool no_free_reg(Insn_t ir) { return !ir.rd() || ir.rd()==NOREG || numfree==max_phy_regs-64; };
  void rename_input_regs(Insn_t& ir);
  bool ready_insn(Insn_t ir);
  void commit_insn(Insn_t& ir);
  void acquire_reg(uint8_t r);
  void release_reg(uint8_t r);
  
  uintptr_t get_state();
  void put_state(uintptr_t pc);

  bool can_execute_insn(Insn_t ir, uintptr_t pc);
  bool issue_from_queue();
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
  void interactive();
};

long ooo_riscv_syscall(hart_t* h, long a0);
int clone_proxy(hart_t* h);

extern uint8_t latency[];
