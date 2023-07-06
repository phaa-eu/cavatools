#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

#MINUS_O := -Ofast -Wswitch
#MINUS_O := -O0 -DDEBUG -Wswitch
#MINUS_O := -O -DDEBUG -Wswitch
MINUS_O := -O -Wswitch

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
HEADERS := options.h opcodes.h caveat.h

libfiles := options.o instructions.o loader.o decoder.o proxy_syscall.o interpreter.o singlestep.o strand.o
bins := uspike.o gdblink.o $(libfiles)

# Compiling options
CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include
CFLAGS := -I$(RVTOOLS)/riscv-gnu-toolchain/ -g -O0
LIBS := ~/lib/libsoftfloat.a
#LDFLAGS := -Wl,-Ttext=70000000
LDFLAGS := -Wl,-Ttext=70000000 -static

uspike: $(bins)
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) $(LIBS)

# Dependencies

uspike.o instructions.o strand.o interpreter.o: caveat.h opcodes.h options.h
instructions.o: decoder.h constants.h
interpreter.o:  strand.h semantics.h arithmetic.h
strand.o: strand.h
proxy_syscall.o: ecall_nums.h
gdblink.o:  caveat.h strand.h

# Python scripts to create various files
#ecall_nums.h: Makefile syscall_mapping
ecall_nums.h: Makefile munge_syscalls.py
	python munge_syscalls.py > ecall_nums.h
#	./syscall_mapping < syscalls-riscv64 > $@

# Create riscv opcodes
opcodes.h:
	make -C ../opcodes

# Cleanup & installation

clean: tidy
	rm -f *.o *~ ./#*# *.tmp

tidy:
	rm -f uspike opcodes.h constants.h decoder.h ecall_nums.h semantics.h
	rm -rf $(CAVA)/include/cava
	rm -f $(CAVA)/lib/libcava.a

install: uspike
	cp uspike $(CAVA)/bin/.
	ar r $(CAVA)/lib/libcava.a $(libfiles)
	mkdir -p $(CAVA)/include/cava
	cp $(HEADERS) $(CAVA)/include/cava/.
