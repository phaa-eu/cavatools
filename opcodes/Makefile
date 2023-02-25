#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

install: custom_asm_macros.h isa.json ../caveat/opcodes.h
	cp custom_asm_macros.h $(CAVA)/include/

custom_asm_macros.h: isa.json munge_gcc.py
	python munge_gcc.py /opt/riscv-gnu-toolchain

../caveat/opcodes.h isa.json:  isa.def crunch.py
	python crunch.py < isa.def

clean:
	rm -f *.o *~ ./#*#
	rm -f isa.json custom_asm_macros.h

