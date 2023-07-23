#
#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
#  Environment variables RVTOOLS and CAVA must be defined

.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

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

install:
	make -C softfloat
	make -C spike    install
	make -C opcodes  install
	make -C caveat   install
	make -C cachesim install





