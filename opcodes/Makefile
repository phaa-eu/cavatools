#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

install:  isa.def crunch.py munge_gcc.py
	python munge_gcc.py < isa.def
	python crunch.py < isa.def

clean:
	rm -f *.o *~ ./#*#
