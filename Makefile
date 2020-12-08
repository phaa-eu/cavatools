.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

clean:
	rm -f $(HOME)/lib/libcava.a $(HOME)/lib/softfloat.a *~ ./#*#
	(cd $(HOME)/bin; rm -f caveat cachesim pipesim traceinfo )
	make -C softfloat clean
	make -C caveat    clean
	make -C cachesim  clean
	make -C pipesim   clean
	make -C traceinfo clean


tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )


install:
	make -C softfloat install
	make -C caveat    install
	make -C cachesim  install
	make -C pipesim   install
	make -C traceinfo install
	make -C utilities/softpipe install





