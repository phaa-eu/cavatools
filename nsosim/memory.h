/*
  Copyright (c) 2025 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#define memory_word_size 	8	// in bytes
#define memory_channels		1
#define memory_banks 		8

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


      
void clock_memory_system();
void display_memory_system(WINDOW* w, int y, int x);

//extern thread_local Memory_t memory[memory_channels][memory_banks];
extern Memory_t memory[memory_channels][memory_banks];

