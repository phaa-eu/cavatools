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
	make -C opcodes   clean
	make -C uspike    clean
	make -C caveat    clean
	make -C erised    clean

tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )

install:
	make -C opcodes install
	make -C uspike  install
	make -C caveat  install
	make -C erised  install





