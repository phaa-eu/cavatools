/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define thread_local

const int issue_queue_length = 4;
const int lsq_length = 2;	// load-store queue

const int dispatch_history = 4096;
const int cycle_history = 4*dispatch_history;

// timing wheel for simulating pipelines
const int num_write_ports = 1;
const int max_latency = 32;	// for timing wheel
extern uint8_t latency[];	// for each opcode

typedef uintptr_t Addr_t;
typedef uint8_t Reg_t;


// Unified physical register file + load-store queue
//   First part holds physical registers with a free list.
//   Second part holds lsq entries.
//
const int max_phy_regs = 64 + issue_queue_length;
const int regfile_size = max_phy_regs + lsq_length;

inline bool is_store_buffer(uint8_t r) {
  return max_phy_regs<=r && r<max_phy_regs+lsq_length;
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


class History_t {		// dispatched instruction
public:
  long long clock;		// at this time
  Insn_t insn;			// with renamed registers
  Addr_t pc;			// at this PC
  Insn_t ref;			// original instruction (for display)
#ifdef VERIFY
  uintptr_t expected_rd;	// for checking against uspike
  uintptr_t actual_rd;		// for display
  uintptr_t expected_pc;
#endif
  enum Status_t { Retired, Executing, Queued, Queued_stbchk, Queued_noport, Queued_nochk, Dispatch } status;
  Reg_t lsqpos;			// address in register here
  
  void display(WINDOW* w, class Core_t*);
};


class Port_t {
  uintptr_t _addr;		// address of memory reference
  int _latency;			// of operation
  History_t* _history;		// on behalf of this instruction
  bool _active;			// valid pending request
public:
  bool active() { return _active; }
  int latency() { return _latency; }
  uintptr_t addr() { return _addr; }
  History_t* history() { return _history; }

  void request(uintptr_t a, long long l, History_t* h) { _active=true; _addr=a; _latency=l; _history=h; }
  void deactivate() { _active=false; }
  void display(WINDOW* w, int y, int x, class Core_t* c);
};


extern long long cycle;		// count number of processor cycles
//extern thread_local long long cycle;      // count number of processor cycles
extern thread_local long long mismatches; // count number of mismatches

class Core_t : public hart_t {
  long long _insns;		// count number of instructions executed
  long long _inflight;		// number of instructions in flight
public:
  simulator_state_t s;		// replaces uspike state
  // physical register file map
  uint8_t regmap[256];		// architectural to physical
  bool busy[regfile_size];	// register waiting for execution value
  unsigned uses[regfile_size];	// reference count
private:
  uint8_t freelist[max_phy_regs];
  int numfree;			// maintained as stack
  
  // Store buffer implemented within physical register file structure to share code.
  // It consists of a range of registers with their busy[] and uses[] entries.
  // The register value s.reg[].a holds the address.
  
  int lsqtail;			// first available entry in circular store buffer
  bool lsq_active(Reg_t r);	// this lsq entry is in use
  bool lsqbuf_full();
  Reg_t allocate_lsq_entry(uintptr_t addr, bool is_store);

  Port_t port;			// memory port

  // issue queue
  History_t* queue[issue_queue_length];
  int last;			// queue depth

  // pipeline model for instruction finish time
  History_t* wheel[max_latency+1];
  int index(unsigned k) { assert(k<=max_latency); return (cycle+k)%(max_latency+1); }

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
  bool no_free_reg(Insn_t ir) { return numfree==max_phy_regs-64; };
  void show_reg(WINDOW* w, Reg_t n, char sep, int ref);
  
  void rename_input_regs(Insn_t& ir);
  void rename_output_reg(Insn_t& ir);
  bool ready_insn(Insn_t ir);
  void acquire_reg(uint8_t r);
  void release_reg(uint8_t r);
  Reg_t check_store_buffer(uintptr_t addr, Reg_t k =NOREG); // k=start+1 position
  bool lsq_full();
  
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

void display_history(WINDOW* w, int y, int x, Core_t* c, int lines);
