
# Path where things should be installed
R = /home/peterhsu

CFLAGS   = -I$R/include/ -g -Ofast
#yCFLAGS   = -I$R/include/ -g -O0
LIBS     = $R/lib/libcava.a -lrt -lpthread
LDFLAGS  = -Wl,-Ttext=70000000

pipesim:  pipesim.o fast_pipe.o slow_pipe.o $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

pipesim.o fast_pipe.o slow_pipe.o:  pipesim.h $R/include/opcodes.h $R/include/insn.h $R/include/shmfifo.h $R/include/caveat.h

fast_pipe.o slow_pipe.o:  mainloop.h
fast_pipe.o slow_pipe.o:  cache_4way.h

cache_1way.h:  make_cache
	./make_cache 0

cache_2way.h:  make_cache
	./make_cache 1

cache_4way.h:  make_cache
	./make_cache 2

install:  pipesim
	-cp $^ $R/bin/


.PHONY:
clean:
	rm -f pipesim cache_?way.h *.o *~ ./#*#
