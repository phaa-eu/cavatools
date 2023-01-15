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
#MINUS_O := -O0
#MINUS_O := -O0 -DDEBUG
#MINUS_O := -O3 -DDEBUG
MINUS_O := -O3 -Wswitch

# Paths to riscv-tools build directories on local system
B := $(RVTOOLS)/riscv-isa-sim
I := -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
HEADERS := options.h opcodes.h uspike.h instructions.h mmu.h hart.h

# Compiling options

libfiles := options.o instructions.o elf_loader.o proxy_syscall.o interpreter.o hart.o
bins := main.o gdblink.o $(libfiles)

#CXXFLAGS := $I -g $(MINUS_O)
CXXFLAGS := -g $(MINUS_O)
CFLAGS := -I$(RVTOOLS)/riscv-gnu-toolchain/ -g -O0
#LIBS := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl
LIBS := ~/lib/libsoftfloat.a
#LIBS := $(RVTOOLS)/riscv-isa-sim/build/libsoftfloat.a
LDFLAGS := -Wl,-Ttext=70000000

uspike: $(bins)
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) $(LIBS)

#$(CAVA)/lib/libcava.a:  mmu.h
#	make -j 16 -C insns
#	mv libcava.a $@

# Dependencies

main.o instructions.o interpreter.o: uspike.h opcodes.h instructions.h
main.o options.o: options.h
instructions.o: decoder.h constants.h 
elf_loader.o proxy_syscall.o gdblink.o: elf_loader.h
interpreter.o:  hart.h
hart.o: hart.h
hart.o decoder.h dispatch_table.h fastops.h: opcodes.h hart.h
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
	rm -f uspike opcodes.h decoder.h dispatch_table.h constants.h ecall_nums.h fastops.h
	rm -f $(CAVA)/lib/libcava.a

install: uspike
	cp uspike $(CAVA)/bin/.
	ar r $(CAVA)/lib/libcava.a $(libfiles)
	mkdir -p $(CAVA)/include/cava
	cp $(HEADERS) $(CAVA)/include/cava/.
