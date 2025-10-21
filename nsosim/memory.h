/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define memory_word_size 	8	// in bytes
#define memory_channels		1
#define memory_banks 		8

#define mem_channel(a)  ((a/memory_word_size) % memory_channels)
#define mem_bank(a)  ((a/memory_channels/memory_word_size) % memory_banks)

class Memory_t {		// Model memory bank operation
  Addr_t addr;			// at this address
  long long finish;		// cycle when finished
  bool _active;
  History_t* h;			// instruction this belongs to
 public:
  bool active() { return _active; }
  //void check(Addr_t a) { assert(a == addr); }

  void display(WINDOW* w);
  void show_as_port(WINDOW* w);
  friend void clock_memory_system(Core_t* cpu);
  friend Memory_t make_mem_descr(Addr_t a, long long f, History_t* h);
};

void clock_memory_system();
Memory_t make_mem_descr(Addr_t a, long long f, History_t* h);
void display_memory(WINDOW* w, int y, int x);

extern thread_local Memory_t memory[memory_channels][memory_banks];
extern thread_local Memory_t port[memory_channels];

