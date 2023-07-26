#
#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
#  Environment variables RVTOOLS and CAVA must be defined

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
ifndef CAVA
CAVA := $(HOME)
endif

.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

install:
	make -j 16 -C softfloat
	cp softfloat/libsoftfloat.a ~/lib/
	make -C spike    install
	make -C opcodes  install
	make -C caveat   install
	make -C cachesim install

clean:
	rm -f $(CAVA)/lib/libcava.a *~ ./#*#
	rm -f $(CAVA)/include/cava/*
	make -C softfloat clean
	make -C spike     clean
	make -C opcodes   clean
	make -C caveat    clean
	make -C cachesim  clean

tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )





