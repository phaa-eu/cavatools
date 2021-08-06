.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

clean:
	rm -f $(HOME)/lib/libcava.a $(HOME)/lib/softfloat.a *~ ./#*#
	( cd softfloat/build/Linux-x86_64-GCC; rm -f softfloat.a *.o )
	make -C caveat    clean
#	make -C cachesim  clean
	make -C erised    clean
	make -C uspike    clean

#	(cd $(HOME)/bin; rm -f caveat cachesim pipesim traceinfo )



tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )


install:
	( cd softfloat/build/Linux-x86_64-GCC; make; cp softfloat.a $(HOME)/lib )
	cp -rp softfloat/source/include $(HOME)/include/softfloat
	make -C caveat    install
#	make -C cachesim  install
	make -C erised    install
	make -C utilities/softpipe install





