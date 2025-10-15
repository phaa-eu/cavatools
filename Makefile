#
#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
#  Environment variables RVTOOLS and CAVA must be defined

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
ifndef CAVA
CAVA := $(HOME)
endif



# Comment this out to create cavatools without Spike
#export spike := -DSPIKE



.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

install:
	make -j 16 -C softfloat install
	make -C spike    install
	make -C opcodes  install
	make -C caveat   install
	make -C cachesim install
	make -j 16 -C nsosim   install

clean:
	rm -f $(CAVA)/lib/libcava.a *~ ./#*#
	rm -f $(CAVA)/include/cava/*
	rm -f softfloat/libsoftfloat.a
	make -C softfloat clean
	make -C spike     clean
	make -C opcodes   clean
	make -C caveat    clean
	make -C cachesim  clean
	make -C nsosim    clean

tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )





