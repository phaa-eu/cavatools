#
#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

MINUS_O := -Ofast
#MINUS_O := -O0 -DDEBUG

# Paths to riscv-tools build directories on local system
B := $(RVTOOLS)/riscv-isa-sim
I := -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
HEADERS := options.h opcodes.h uspike.h instructions.h mmu.h hart.h

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

libfiles := options.o instructions.o elf_loader.o proxy_syscall.o interpreter.o hart.o
bins := main.o gdblink.o $(libfiles)

CXXFLAGS := $I -g $(MINUS_O)
CFLAGS := -I$(RVTOOLS)/riscv-gnu-toolchain/ -g -O0
LIBS := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl
LDFLAGS := -Wl,-Ttext=70000000

uspike: $(bins) $(CAVA)/lib/libcava.a
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) $(CAVA)/lib/libcava.a $(LIBS)

$(CAVA)/lib/libcava.a:  mmu.h
	make -j 16 -C insns
	mv libcava.a $@

# Dependencies

main.o instructions.o interpreter.o: uspike.h opcodes.h instructions.h
main.o options.o: options.h
instructions.o: decoder.h constants.h 
elf_loader.o proxy_syscall.o gdblink.o: elf_loader.h
interpreter.o:  dispatch_table.h fastops.h hart.h
hart.o: hart.h
hart.o decoder.h dispatch_table.h fastops.h: opcodes.h hart.h
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

clean: tidy
	rm -f *.o *~ ./#*# *.tmp
	rm -f insns/*.o insns/*.cc insns/*~ insns/#*#

tidy:
	rm -f uspike opcodes.h decoder.h dispatch_table.h constants.h ecall_nums.h fastops.h
	rm -f $(CAVA)/lib/libcava.a

install: uspike
	cp uspike $(CAVA)/bin/.
	ar r $(CAVA)/lib/libcava.a $(libfiles)
	mkdir -p $(CAVA)/include/cava
	cp $(HEADERS) $(CAVA)/include/cava/.
