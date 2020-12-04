# Path where things should be installed
R = $(HOME)
B = build

_dummy := $(shell mkdir -p $B)
_dummy := $(shell mkdir -p $R/include/cava)

# Object files for libcava.a
aobj := elf_loader.o insn.o shmfifo.o
aobj := $(addprefix $B/,$(aobj))

# Object files for caveat not in libcava.a
cobj := main.o core.o fast_sim.o slow_sim.o ecall.o
cobj := $(addprefix $B/,$(cobj))

# Include files for libcava.a
incf := opcodes.h insn.h caveat.h shmfifo.h

# Libraries
libs := $R/lib/softfloat.a -lrt -lpthread -lm

# Compiler flags
CFLAGS := -I../include -I$R/include -I$R/include/softfloat -g -O3
LDFLAGS	:= -Wl,-Ttext=70000000


#$(info $$aobj is [${aobj}])
#$(info $$cobj is [${cobj}])


# Make targets

all:  $R/lib/libcava.a $B/caveat

install:  all
	-cp $B/caveat  $R/bin/caveat
	-cp $(incf)  $R/include/cava

.PHONY:  clean
clean:
	rm -f decode_insn.h disasm_insn.h execute_insn.h ecall_nums.h opcodes.h opcodes_attr.h *.o *~ ./#*#
	rm -rf build


# Emulator and library

$B/caveat : $(cobj) $R/lib/libcava.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(libs)

$R/lib/libcava.a : $(aobj)
	ar rcs $@ $^

# Dependencies

$(aobj):  $(incf)
$(cobj):  $(incf)

$B/slow_sim.o $B/fast_sim.o:  sim_body.h execute_insn.h caveat_fp.h core.h
$B/ecall.o:  core.h ecall_nums.h
$B/insn.o:  decode_insn.h disasm_insn.h
$B/trace.o:  fifo.h

$B/fast_sim.o:  fast_sim.c
	$(CC) $(CFLAGS) -Ofast -o $@ -c $<

$B/core.o:  core.c core.h
	$(CC) $(CFLAGS) -o $@ -c $<

$B/elf_loader.o : elf_loader.c
	$(CC) $(CFLAGS) -o $@ -c $<

$B/ecall.o : ecall.c
	$(CC) $(CFLAGS) -o $@ -c $<

$B/%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

$B/%.o : %.cc
	$(CC) $(CFLAGS) -o $@ -c $<


opcodes.h opcodes_attr.h decode_insn.h execute_insn.h disasm_insn.h constants.c:  crunch_isa.py Instructions.def
	python3 crunch_isa.py

ecall_nums.h:  make_ecall_tbl.py
	python3  make_ecall_tbl.py


