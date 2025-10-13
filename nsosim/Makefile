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

MINUS_O := -g -O0

#CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include/cava -I$(CAVA)/include/softfloat -I/../spike
#CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include/softfloat -I../caveat -I../spike -DDEBUG
CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include/softfloat -I../caveat -I../spike

BINS := display.o core.o perform.o nsosim.o
LIBS := $(CAVA)/lib/libcava.a ~/lib/libsoftfloat.a
LDFLAGS := -Wl,-Ttext=70000000 -lncurses

nsosim:  $(BINS) $(LIBS)
	g++ -o nsosim $(BINS) $(LDFLAGS) $(LIBS)
#	g++ -o nsosim $(BINS) $(LDFLAGS) $(LIBS) -ldl -lrt

#perform.o: ~/include/cava/caveat.h ~/include/cava/hart.h

perform.o nsosim.o:  core.h

install:  nsosim
	cp nsosim $(CAVA)/bin/.

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f lru_fsm_?way.h
	rm -f nsosim


