
.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

clean:
	rm -f $(home)/lib/libcava.a *~ ./#*#
	make -C caveat    clean
	make -C cachesim  clean
	make -C pipesim   clean
	make -C traceinfo clean


tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )


install:
	make -C caveat    install
	make -C cachesim  install
	make -C pipesim   install
	make -C traceinfo install





