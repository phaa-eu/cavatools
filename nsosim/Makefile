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

MINUS_O := -g -O

#CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include/cava -I$(CAVA)/include/softfloat -I/../spike
#CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include/softfloat -I../caveat -I../spike -DDEBUG
CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include/softfloat -I../caveat -I../spike

BINS := perform.o nsosim.o
LIBS := $(CAVA)/lib/libcava.a ~/lib/libsoftfloat.a
LDFLAGS := -Wl,-Ttext=70000000

install:  nsosim
	cp nsosim $(CAVA)/bin/.

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f lru_fsm_?way.h
	rm -f nsosim


nsosim:  $(BINS) $(LIBS)
	g++ -o nsosim $(BINS) $(LDFLAGS) $(LIBS)
#	g++ -o nsosim $(BINS) $(LDFLAGS) $(LIBS) -ldl -lrt

#perform.o: ~/include/cava/caveat.h ~/include/cava/hart.h

perform.o nsosim.o:  core.h

cache.o: lru_fsm_1way.h lru_fsm_2way.h lru_fsm_3way.h lru_fsm_4way.h lru_fsm_5way.h lru_fsm_6way.h

lru_fsm_1way.h: make_cache
	./make_cache 1

lru_fsm_2way.h: make_cache
	./make_cache 2

lru_fsm_3way.h: make_cache
	./make_cache 3

lru_fsm_4way.h: make_cache
	./make_cache 4

lru_fsm_5way.h: make_cache
	./make_cache 5

lru_fsm_6way.h: make_cache
	./make_cache 6

