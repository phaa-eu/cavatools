


# Path where things should be installed
R = ~
B = build
S = ../symlinks

# Object files for libcava.a
aobj := elf_loader.o insn.o

# Include files for libcava.a
incf := opcodes.h insn.h caveat.h shmfifo.h

# Object files for caveat not in libcava.a
cobj := main.o core.o fast_sim.o slow_sim.o ecall.o

# Compiler flags
cflags := -g -O3

CFLAGS := $(cflags)
CXXFLAGS := $(cflags)
FASTFLAGS := $(cflags) -Ofast

LIBS := $S/SoftFloat-3e/build/Linux-x86_64-GCC/softfloat.a -lrt -lpthread -lm
LDFLAGS	:= -Wl,-Ttext=70000000

# Text substitutions
aobj := $(addprefix $B/,$(aobj))
cobj := $(addprefix $B/,$(cobj))

#$(info $$aobj is [${aobj}])
#$(info $$cobj is [${cobj}])


# Make targets

all:  $B/libcava.a $B/caveat

install:  all
	-cp $B/caveat  $R/bin/caveat
	-cp $B/libcava.a  $R/lib/libcava.a
	-cp $(incf)  $R/include/

.PHONY:  clean
clean:
	rm -f decode_insn.h disasm_insn.h execute_insn.h ecall_nums.h opcodes.h opcodes_attr.h *.o *~ ./#*#
	( cd build; rm -f * )


# Emulator and library

$B/caveat : $(cobj) $B/libcava.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$B/libcava.a : $(aobj)
	ar rcs $@ $^

# Dependencies

$(aobj):  $(incf)
$(cobj):  $(incf)

$B/slow_sim.o $B/fast_sim.o:  sim_body.h execute_insn.h caveat_fp.h core.h
$B/ecall.o:  core.h ecall_nums.h
$B/insn.o:  decode_insn.h disasm_insn.h
$B/trace.o:  fifo.h

$B/%_sim.o:  %_sim.c
	$(CC) $(FASTFLAGS) -o $@ -c $< -I$S/SoftFloat-3e/source/include

$B/core.o:  core.c core.h
	$(CC) $(CFLAGS) -o $@ -c $< -I$S/SoftFloat-3e/source/include

$B/elf_loader.o : elf_loader.c
	$(CC) $(CFLAGS) -o $@ -c $< \
		-I$S/riscv-gnu-toolchain/riscv-binutils \
		-I$S/riscv-gnu-toolchain/riscv-binutils/include \
		-I$S/riscv-gnu-toolchain/build/build-binutils-linux/bfd

$B/ecall.o : ecall.c
	$(CC) $(CFLAGS) -o $@ -c $< \
	       -I$S/riscv-isa-sim/riscv \
	       -I$S/SoftFloat-3e/source/include

$B/%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

$B/%.o : %.cc
	$(CC) $(CFLAGS) -o $@ -c $<


opcodes.h opcodes_attr.h decode_insn.h execute_insn.h disasm_insn.h constants.c:  crunch_isa.py Instructions.def
	python3 crunch_isa.py

ecall_nums.h:  make_ecall_tbl.py
	python3  make_ecall_tbl.py


