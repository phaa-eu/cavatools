CXXFLAGS = -g -O3

install:
	cache.o

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f test_cache lru_fsm_?way.h

test_cache:  test_cache.o cache.o
	$(CXX) $(CXXFLAGS) test_cache.o cache.o -o test_cache


test_cache.o: test_cache.cc cache.h lru_fsm_1way.h lru_fsm_2way.h lru_fsm_3way.h lru_fsm_4way.h

cache.o:  cache.h

lru_fsm_1way.h: make_cache
	./make_cache 1

lru_fsm_2way.h: make_cache
	./make_cache 2

lru_fsm_3way.h: make_cache
	./make_cache 3

lru_fsm_4way.h: make_cache
	./make_cache 4

