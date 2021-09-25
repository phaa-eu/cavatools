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
I := $(CAVA)/include/cava

CXXFLAGS := -I$I -g -O0

LIBS := $(CAVA)/lib/libcava.a $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl -lrt -lncurses -lX11
LDFLAGS := -Wl,-Ttext=70000000

# Dependent headers
#hdrs := I$/options.h $I/uspike.h $I/perf.h




erised:  erised.o $(LIBS)
#erised:  viewer.o $(LIBS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

install:  erised
	-cp $^ $(CAVA)/bin

.PHONY:
clean:
	rm -f erised *.o *~ ./#*#
