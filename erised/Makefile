
.PHONY: nothing install clean
nothing:
	echo "clean install?"

install:
	make -C pipesim install
	make -C cachesim install
	make -C traceinfo traceinfo
	make -C softpipe softpipe

clean:
	make -C pipesim clean
	make -C cachesim clean
	make -C traceinfo clean
	make -C softpipe clean
	rm -f *~ \#*\#
