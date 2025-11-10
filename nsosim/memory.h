/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define memory_word_size 	8	// in bytes
#define memory_channels		1
#define memory_banks 		4

#define port_queue_length	2

inline unsigned mem_channel(uintptr_t a)  { return (a/memory_word_size) % memory_channels; }
inline unsigned mem_bank(uintptr_t a)  { return (a/memory_channels/memory_word_size) % memory_banks; }

//extern thread_local long long cycle;      // count number of processor cycles
//extern long long cycle;      // count number of processor cycles

class History_t;

class Memory_t {		// Model memory bank operation
  long long _finish;		// cycle when finished
  bool* store_buffer_busy_bit;	// for stores
  bool _active;
  History_t* _history;
 public:
  bool active() { return _active; }
  long long finish() { return _finish; }
  History_t* history() { return _history; }
  void activate(long long f, History_t* h) { assert(!active()); _active=true; _finish=f; _history=h; }
  void deactivate() { _active=false; }
  static void clock_memory_system();
  void display(WINDOW* w, int y, int x);
};




struct port_req_t {
  uintptr_t addr;		// address of memory reference
  int latency;			// of operation
  History_t* history;		// on behalf of this instruction
  Remapping_Regfile_t* regfile;	// for checking write bus reservations
};

class Port_t {
  port_req_t queue[port_queue_length];
  int last;
public:
  Port_t() { last=0; }
  History_t* clock_port();
  bool full() { return last==port_queue_length; }
  bool empty() { return last==0; }
  void request(uintptr_t a, long long l, History_t* h, Remapping_Regfile_t* rf);
  void display(WINDOW* w, int y, int x, class Core_t* c);
};







      
void clock_memory_system();
void display_memory_system(WINDOW* w, int y, int x);

//extern thread_local Memory_t memory[memory_channels][memory_banks];
extern Memory_t memory[memory_channels][memory_banks];

