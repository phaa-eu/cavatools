RVOPS = /opt/BUILD/riscv-tools/riscv-opcodes
RVINS  =/opt/BUILD/riscv-isa-sim/riscv/insns

RV32 := rv32i rv32m rv32a rv32f rv32d system
ISAg := $(RV32) rv64i rv64m rv64a rv64f rv64d
ISAv := rvv
ISAp := rv32b rv32c rv32d-zfh rv32h rv32k rv64b rv64c rv64h rv64k rvk
#ISAc := opcodes-rvc

ISAg := $(addprefix $(RVOPS)/opcodes-, $(ISAg))
ISAv := $(addprefix $(RVOPS)/opcodes-, $(ISAv))
ISAp := $(addprefix $(RVOPS)/opcodes-, $(ISAp))

B := /opt/BUILD/riscv-isa-sim
I :=  -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a
#CXXFLAGS := $I -I$R/include/cava -g -O0
CXXFLAGS := $I -g -O0
LDFLAGS := -Wl,-Ttext=70000000 $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl

uspike: main.o constants.o decoder.o elf_loader.o
	make -j 16 -C insns
	$(CXX) $(CXXFLAGS) -o uspike $^ libinsn.a $(LDFLAGS)

elf_loader.o:  elf_loader.cc
	$(CXX) -g -c elf_loader.cc

main.o constants.o decoder.o: opcodes.h uspike.h

opcodes.h decoder.h constants.cc insns: crunch_isa RV_isa.tmp
	./crunch_isa RV_isa.tmp

RV_isa.tmp: parse_spike RV_bits.tmp RVC.bits
	./parse_spike $(RVINS) RV_bits.tmp RVC.bits > $@

RV_bits.tmp: parse_opcodes
	./parse_opcodes $(ISAg) $(ISAv) $(ISAp) > $@

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f insns/*.o insns/*.cc insns/*~ insns/#*#
	rm -f uspike opcodes.h decoder.h  constants.cc


