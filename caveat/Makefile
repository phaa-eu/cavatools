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

OBJS := main.o core.o perf.o

CXXFLAGS := -I$(CAVA)/include/cava -g -O3 -ffast-math
#CXXFLAGS := -I$(CAVA)/include/cava -g -O0 -DDEBUG
LDFLAGS := -Wl,-Ttext=70000000

install:  caveat perf.o perf.h
	ar r $(CAVA)/lib/libcava.a perf.o
	cp perf.h $(CAVA)/include/cava/.
	cp caveat $(CAVA)/bin/.

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f lru_fsm_?way.h
	rm -f caveat


caveat:  $(OBJS) ../cache/lru_fsm_table.o perf.o $(CAVA)/lib/libcava.a
	$(CXX) $(CXXFLAGS) -o caveat $^ $(LDFLAGS) $L -ldl -lrt

main.o: core.h perf.h
perf.o: perf.h
core.o: core.h ../cache/cache.h

