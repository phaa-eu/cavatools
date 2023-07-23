#  Copyright (c) 2023 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.

I := -I.. -I../../softfloat

CXXFLAGS := $I -g -O3

insn_files := $(wildcard *.cc)
obj_files := $(patsubst %.cc,%.o,$(insn_files))

libspike.a:  $(obj_files)
	ar r $@ $?

$(obj_files):  ../spike_link.h ../processor.h

clean:
	rm -f libspike.a
	rm -f *.cc
	rm -f *.o *~ ./#*# *.tmp

