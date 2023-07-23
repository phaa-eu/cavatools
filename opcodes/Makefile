#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

#ISA := RV64GC-nfp.def
ISA := minimal.def try.def spike.def

install: custom_asm_macros.h isa.json ../caveat/opcodes.h
	cp custom_asm_macros.h $(CAVA)/include/

custom_asm_macros.h: isa.json munge_gcc.py
	python munge_gcc.py /opt/riscv-gnu-toolchain

../caveat/opcodes.h isa.json:  $(ISA) crunch.py spike.def Makefile
	python crunch.py $(ISA)

clean:
	rm -f *.o *~ ./#*#
	rm -f isa.json custom_asm_macros.h

