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

R := /opt/BUILD
B := $R/riscv-isa-sim
I := -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a
#CXXFLAGS := $I -I$R/include/cava -g -O0
CXXFLAGS := $I -g -O0
CFLAGS := -g -O0
LIBS := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a -ldl
LDFLAGS := -Wl,-Ttext=70000000

bins := main.o options.o decoder.o elf_loader.o proxy_syscall.o interpreter.o gdblink.o

uspike: $(bins) libcava.a
	$(CXX) $(CXXFLAGS) -o uspike $(bins) $(LDFLAGS) libcava.a $(LIBS)

libcava.a:  opcodes.h
	make -j 16 -C insns


#elf_loader.o:  elf_loader.c
#	$(CC) $(CFLAGS) -I$R/riscv-gnu-toolchain/linux-headers/include/linux/ -g -c elf_loader.c

main.o decoder.o: uspike.h opcodes.h
main.o options.o: options.h
#decoder.o: decoder.h
elf_loader.o proxy_syscall.o gdblink.o: processinfo.h interpreter.h
interpreter.o:  ecall_nums.h dispatch_table.h

ecall_nums.h:
	./syscall_mapping $R/riscv-pk/pk/syscall.h $B/fesvr/syscall.h

opcodes.h decoder.h constants.h dispatch_table.h insns: RV_isa.tmp ./crunch_isa
	./crunch_isa RV_isa.tmp

RV_isa.tmp: parse_spike RV_bits.tmp RVC.bits
	./parse_spike $(RVINS) RV_bits.tmp RVC.bits > $@

RV_bits.tmp: parse_opcodes
	./parse_opcodes $(ISAg) > $@
#	./parse_opcodes $(ISAg) $(ISAv) $(ISAp) > $@

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f insns/*.o insns/*.cc insns/*~ insns/#*#
	rm -f uspike opcodes.h decoder.h dispatch_table.h constants.h ecall_nums.h libcava.a


