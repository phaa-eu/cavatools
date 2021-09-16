#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.

#MINUS_O := -Ofast
MINUS_O := -O0 -DDEBUG

# Paths to riscv-tools build directories on local system
RVTOOLS := /opt/riscv-tools
B := $(RVTOOLS)/riscv-isa-sim
I := -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a

# Collect all the opcodes
RVOPS = $(RVTOOLS)/riscv-opcodes
RVINS = $(RVTOOLS)/riscv-isa-sim/riscv/insns

RV32 := rv32i rv32m rv32a rv32f rv32d system
ISAg := $(RV32) rv64i rv64m rv64a rv64f rv64d
ISAv := rvv
ISAp := rv32b rv32c rv32d-zfh rv32h rv32k rv64b rv64c rv64h rv64k rvk

ISAg := $(addprefix $(RVOPS)/opcodes-, $(ISAg))
ISAv := $(addprefix $(RVOPS)/opcodes-, $(ISAv))
ISAp := $(addprefix $(RVOPS)/opcodes-, $(ISAp))

# Compiling options

libfiles := options.o instructions.o elf_loader.o proxy_syscall.o interpreter.o cpu.o memory.o
bins := main.o gdblink.o $(libfiles)

CXXFLAGS := $I -g $(MINUS_O)
CFLAGS := -I$(RVTOOLS)/riscv-gnu-toolchain/ -g -O0
LIBS := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl
LDFLAGS := -Wl,-Ttext=70000000

uspike: $(bins) libcava.a
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) libcava.a $(LIBS)

libcava.a:  mmu.h
	make -j 16 -C insns

# Dependencies

main.o instructions.o interpreter.o: uspike.h opcodes.h
main.o options.o: options.h
instructions.o: decoder.h constants.h 
elf_loader.o proxy_syscall.o gdblink.o: elf_loader.h
interpreter.o:  dispatch_table.h fastops.h cpu.h
cpu.o: cpu.h
cpu.o decoder.h dispatch_table.h fastops.h: opcodes.h cpu.h
proxy_syscall.o: ecall_nums.h

# Python scripts to create various files

ecall_nums.h: Makefile syscall_mapping
	./syscall_mapping < syscalls-riscv64 > $@

opcodes.h:  ISA.tmp make_headers
	./make_headers ISA.tmp

ISA.tmp: ./crunch_isa RV_isa.tmp RV.fast
	./crunch_isa RV_isa.tmp RV.fast > ISA.tmp
#	./crunch_isa RV_isa.tmp > ISA.tmp

RV_isa.tmp: parse_spike RV_bits.tmp RVC.bits
	./parse_spike $(RVINS) RV_bits.tmp RVC.bits > $@

RV_bits.tmp: parse_opcodes
	./parse_opcodes $(ISAg) > $@
#	./parse_opcodes $(ISAg) $(ISAv) $(ISAp) > $@

# Cleanup & installation

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f insns/*.o insns/*.cc insns/*~ insns/#*#
	rm -f uspike opcodes.h decoder.h dispatch_table.h constants.h ecall_nums.h fastops.h libcava.a

install: uspike
	cp uspike $(HOME)/bin/.
	ar r libcava.a $(libfiles)

