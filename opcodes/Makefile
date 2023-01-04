#
#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

../uspike/opcodes.h ../uspike/decoder.h ../uspike/dispatch_table.h ../uspike/constants.h ../uspike/ecall_nums.h ../uspike/fastops.h:  isa.def
	python crunch.py < isa.def

clean:
	rm -f *.o *~ ./#*#
