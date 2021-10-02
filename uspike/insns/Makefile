#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.

# Paths to riscv-tools build directories on local system
RVTOOLS := /opt/riscv-tools
B := $(RVTOOLS)/riscv-isa-sim
I :=  -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns

CXXFLAGS := -I.. $I -g -Ofast
#CXXFLAGS := -I.. $I -g -O0 -DDEBUG

insn_files := $(wildcard *.cc)
obj_files := $(patsubst %.cc,%.o,$(insn_files))

../libcava.a:  $(obj_files)
	ar r ../libcava.a $?

$(obj_files): ../spike_link.h ../mmu.h
