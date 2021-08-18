#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.

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

bins := main.o options.o decoder.o elf_loader.o proxy_syscall.o interpreter.o gdblink.o constants.o

CXXFLAGS := $I -g -O3
CFLAGS := -I$(RVTOOLS)/riscv-gnu-toolchain/ -g -O3
LIBS := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl
LDFLAGS := -Wl,-Ttext=70000000

uspike: $(bins) libcava.a
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) libcava.a $(LIBS)

libcava.a:  mmu.h interpreter.h
	make -j 16 -C insns

# Dependencies

main.o decoder.o interpreter.o: uspike.h opcodes.h
main.o options.o: options.h
decoder.o: decoder.h
elf_loader.o proxy_syscall.o gdblink.o: processinfo.h interpreter.h
interpreter.o:  dispatch_table.h fastops.h mmu.h
constants.o:  ecall_nums.h

decoder.h constants.h dispatch_table.h fastops.h: opcodes.h

# Python scripts to create various files

ecall_nums.h: Makefile syscall_mapping
	./syscall_mapping < syscalls-riscv64 > $@
#	./syscall_mapping $(RVTOOLS)/riscv-gnu-toolchain/linux-headers/include/asm-generic/unistd.h
#	./syscall_mapping $(RVTOOLS)/riscv-pk/pk/syscall.h $B/fesvr/syscall.h


#opcodes.h decoder.h constants.h dispatch_table.h insns &: ./crunch_isa RV_isa.tmp MUST.amend
opcodes.h: ./crunch_isa RV_isa.tmp
	./crunch_isa RV_isa.tmp 
#opcodes.h: ./crunch_isa RV_isa.tmp MUST.amend
#	./crunch_isa MUST.amend RV_isa.tmp 

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

