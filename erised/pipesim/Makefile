
# Path where things should be installed
R = /home/peterhsu

CFLAGS   = -I$R/include/ -g -Ofast
#CFLAGS   = -I$R/include/ -g -O0
LIBS     = $R/lib/libcava.a -lrt -lpthread
LDFLAGS  = -Wl,-Ttext=70000000

pipesim:  pipesim.o fast_pipe.o slow_pipe.o cache.o $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

pipesim.o fast_pipe.o slow_pipe.o:  pipesim.h cache.h $R/include/opcodes.h $R/include/insn.h $R/include/shmfifo.h $R/include/caveat.h

fast_pipe.o slow_pipe.o:  mainloop.h
fast_pipe.o slow_pipe.o pipesim.o:  lru_fsm_4way.h

lru_fsm_1way.h:  make_cache
	./make_cache 1

lru_fsm_2way.h:  make_cache
	./make_cache 2

lru_fsm_3way.h:  make_cache
	./make_cache 3

lru_fsm_4way.h:  make_cache
	./make_cache 4

install:  pipesim
	-cp $^ $R/bin/


.PHONY:
clean:
	rm -f pipesim lru_fsm_?way.h *.o *~ ./#*#
