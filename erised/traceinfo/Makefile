
# Path where things should be installed
R = /home/peterhsu

#CFLAGS   = -I$R/include/ -g -Ofast
CFLAGS   = -I$R/include/ -g -O0
LIBS     = $R/lib/libcava.a -lrt -lpthread
LDFLAGS  = -Wl,-Ttext=70000000

traceinfo:  traceinfo.o $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

cache_4way.h:  make_cache
	./make_cache 2

traceinfo.o:  $R/include/opcodes.h $R/include/insn.h $R/include/shmfifo.h $R/include/caveat.h

install:  traceinfo
	-cp $^ $R/bin/

.PHONY:
clean:
	rm -f traceinfo pipesim cache_?way.h *.o *~ ./#*#
