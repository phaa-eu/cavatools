CXXFLAGS = -g -O3

test_cache:  test_cache.o lru_fsm_table.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_cache.o:  cache.h

lru_fsm_table.o:  cache.h lru_fsm_table.cc

lru_fsm_table.cc:  make_cache
	./make_cache 4

install:  cache.h lru_fsm_table.o

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f test_cache lru_fsm_table.cc
