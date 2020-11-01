
.PHONY:  nothing clean install
nothing:
	echo "clean, tarball, install?"

clean:
	rm -f *~ ./#*#
	make -C caveat clean
	make -C erised clean


tarball:  clean
	( cd ..; tar -czvf cavatools.tgz cavatools )


install:
	make -C caveat install
	make -C erised install





