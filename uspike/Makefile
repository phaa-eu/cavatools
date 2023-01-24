#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

MINUS_O := -Ofast -Wswitch
#MINUS_O := -O3 -DDEBUG -Wswitch
#MINUS_O := -O0 -DDEBUG -Wswitch

#MINUS_O := -O0
#MINUS_O := -O3 -Wswitch

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
#HEADERS := options.h opcodes.h uspike.h instructions.h strand.h mmu.h
HEADERS := options.h opcodes.h instructions.h caveat.h

# Compiling options

libfiles := options.o instructions.o elf_loader.o proxy_syscall.o interpreter.o strand.o
bins := main.o gdblink.o $(libfiles)

CXXFLAGS := -g $(MINUS_O)
CFLAGS := -I$(RVTOOLS)/riscv-gnu-toolchain/ -g -O0
LIBS := ~/lib/libsoftfloat.a
LDFLAGS := -Wl,-Ttext=70000000

uspike: $(bins)
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) $(LIBS)

# Dependencies

main.o instructions.o strand.o interpreter.o: caveat.h opcodes.h options.h
main.o instructions.o interpreter.o: caveat.h opcodes.h instructions.h
main.o options.o: options.h
instructions.o: instructions.h decoder.h constants.h
elf_loader.o proxy_syscall.o gdblink.o: elf_loader.h
interpreter.o:  strand.h semantics.h
strand.o: strand.h
proxy_syscall.o: ecall_nums.h

# Python scripts to create various files
ecall_nums.h: Makefile syscall_mapping
	./syscall_mapping < syscalls-riscv64 > $@

# Create riscv opcodes
opcodes.h:
	make -C ../opcodes

# Cleanup & installation

clean: tidy
	rm -f *.o *~ ./#*# *.tmp

tidy:
	rm -f uspike opcodes.h decoder.h semantics.h constants.h ecall_nums.h
	rm -f $(CAVA)/lib/libcava.a

install: uspike
	cp uspike $(CAVA)/bin/.
	ar r $(CAVA)/lib/libcava.a $(libfiles)
	mkdir -p $(CAVA)/include/cava
	cp $(HEADERS) $(CAVA)/include/cava/.
