/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

extern long long cycle;		// count number of processor cycles

const int issue_queue_length = 16;

const int dispatch_history = 4096;
const int cycle_history = 4*dispatch_history;

typedef uintptr_t Addr_t;
typedef uint8_t Reg_t;

const int store_buffer_length = 8;

// timing wheel for simulating pipelines
const int num_write_ports = 1;
const int max_latency = 32;	// for timing wheel
extern uint8_t latency[];	// for each opcode

// Unified physical register file + load-store queue
//   First part holds physical registers with a free list.
//   Second part holds lsq entries.
//
const int max_phy_regs = 64 + issue_queue_length + 16;
const int regfile_size = max_phy_regs + store_buffer_length;




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
  enum Status_t { Retired, Immediate, Executing, Queued, Queued_stbchk, Queued_noport, Queued_nochk, Dispatch } status;
  Reg_t stbpos;			// store address in register here
  
  void display(WINDOW* w, class Core_t*);
};


// Store buffer implemented within physical register file structure to share code.
// It consists of a range of registers with their busy[] and uses[] entries.
// The register value s.reg[].a holds the address.

class Remapping_Regfile_t {
  uint8_t regmap[256];		// architectural to physical
  bool _busy[regfile_size];	// register waiting for execution value
  unsigned _uses[regfile_size];	// reference count
  uint8_t freelist[max_phy_regs];
  int numfree;			// maintained as stack
  int stbtail;			// first available entry in circular store buffer

  // pipeline model for instruction finish time
  History_t* wheel[max_latency+1];
  int index(unsigned k) { assert(k<=max_latency); return (cycle+k)%(max_latency+1); }
public:
  void reset();
  
  Reg_t map(Reg_t r) { return regmap[r]; }
  bool busy(int r) { return _busy[r]; }
  int  uses(int r) { return _uses[r]; }

  bool no_free_reg() { return numfree==0; }
  bool bus_busy(int n) { return wheel[index(n)] != 0; }
  History_t* simulate_write_reg() { History_t* h=wheel[index(0)]; wheel[index(0)]=0; return h; }
  
  void acquire_reg(uint8_t r) { if (r!=NOREG) ++_uses[r]; }
  void release_reg(uint8_t r);
  
  Reg_t rename_reg(Reg_t arch_reg);
  //  bool reserve_bus(int latency, History_t* h);
  void reserve_bus(int latency, History_t* h);
  
  //  History_t* write_slot(int k =0) { return wheel[index(k)]; }
  //  bool write_port_busy(int k =0) { return write_slot(k) != 0; }
  void value_is_ready(Reg_t r) { _busy[r] = false; }

  Reg_t stbuf(int k =0) {
    assert(0 <= stbtail && stbtail < store_buffer_length);
    return (stbtail-k+store_buffer_length) % store_buffer_length + max_phy_regs;
  }
  bool store_buffer_full() { return _uses[stbuf()] > 0; }
  Reg_t allocate_store_buffer();
  
  friend void display_history(WINDOW* w, int y, int x, Core_t* c, int lines);
};

inline bool is_store_buffer(uint8_t r) {
  return max_phy_regs<=r && r<max_phy_regs+store_buffer_length;
}

void display_history(WINDOW* w, int y, int x, Core_t* c, int lines);
