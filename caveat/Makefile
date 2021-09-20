#
#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

B := $(RVTOOLS)/riscv-isa-sim
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a

CXXFLAGS := -I$(CAVA)/include/cava -g -Ofast
#CXXFLAGS := -I$(CAVA)/include/cava -g -O0 -DDEBUG
LDFLAGS := -Wl,-Ttext=70000000

install:  caveat perf.o perf.h
	cp caveat $(CAVA)/bin/.
	ar r $(CAVA)/lib/libcava.a perf.o
	cp perf.h $(CAVA)/include/cava/.

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f lru_fsm_?way.h
	rm -f caveat


caveat:  simulator.o cache.o perf.o $(CAVA)/lib/libcava.a
	g++ -o caveat $^ $(LDFLAGS) $L -ldl -lrt

cache.o simulator.o:  cache.h
perf.o simulator.o: perf.h

simulator.o: lru_fsm_1way.h lru_fsm_2way.h lru_fsm_3way.h lru_fsm_4way.h

lru_fsm_1way.h: make_cache
	./make_cache 1

lru_fsm_2way.h: make_cache
	./make_cache 2

lru_fsm_3way.h: make_cache
	./make_cache 3

lru_fsm_4way.h: make_cache
	./make_cache 4

