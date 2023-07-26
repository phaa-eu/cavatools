#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#

# Comment this out to create cavatools without Spike
#spike := -DSPIKE

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
ifndef CAVA
CAVA := $(HOME)
endif

MINUS_O := -Ofast -Wswitch
#MINUS_O := -O0 -DDEBUG -Wswitch
#MINUS_O := -O -DDEBUG -Wswitch
#MINUS_O := -O -Wswitch

HEADERS := options.h opcodes.h caveat.h

libfiles := options.o instructions.o loader.o decoder.o proxy_syscall.o interpreter.o hart.o ../spike/processor.o 
bins := uspike.o gdblink.o $(libfiles)

# Compiling options
CXXFLAGS := -g $(MINUS_O) -I$(CAVA)/include $(spike) -I../softfloat -I../spike

LIBS := $(CAVA)/lib/libspike.a $(CAVA)/lib/libsoftfloat.a
LDFLAGS := -Wl,-Ttext=70000000 -static

uspike: $(bins) $(LIBS)
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) $(LIBS)

# Dependencies

uspike.o instructions.o hart.o interpreter.o: caveat.h opcodes.h options.h
instructions.o: decoder.h constants.h
decoder.o:  decoder.h
interpreter.o:  hart.h semantics.h arithmetic.h spike_insns.h
hart.o: hart.h
proxy_syscall.o: ecall_nums.h
gdblink.o:  caveat.h hart.h

../spike/insns/libspike.a spike_insns:
	make -C ../spike

# Python scripts to create various files

ecall_nums.h: Makefile munge_syscalls.py
	python munge_syscalls.py > ecall_nums.h

# Create riscv opcodes

opcodes.h spike_insns.h:  ../opcodes/spike.def
	make -C ../spike
	make -C ../opcodes

# Cleanup & installation

clean: tidy
	rm -f *.o *~ ./#*# *.tmp

tidy:
	rm -f uspike opcodes.h constants.h decoder.h ecall_nums.h semantics.h spike_insns.h
	rm -rf $(CAVA)/include/cava
	rm -f $(CAVA)/lib/libcava.a

install: uspike
	cp uspike $(CAVA)/bin/.
	ar r $(CAVA)/lib/libcava.a $(libfiles)
	mkdir -p $(CAVA)/include/cava
	cp $(HEADERS) $(CAVA)/include/cava/.
