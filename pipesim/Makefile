# Path where things should be installed
R = $(HOME)

CFLAGS   = -I$R/include/cava -g -Ofast
LIBS     = $R/lib/libcava.a -lrt -lpthread
LDFLAGS  = -Wl,-Ttext=70000000

# Dependent headers
hdrs := opcodes.h insn.h shmfifo.h caveat.h

# Text substitutions
hdrs := $(addprefix $R/include/cava/,$(hdrs))


pipesim:  pipesim.o fast_pipe.o slow_pipe.o count_pipe.o cache.o $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

pipesim.o fast_pipe.o slow_pipe.o count_pipe.o:  pipesim.h cache.h $(hdrs)

fast_pipe.o slow_pipe.o count_pipe.o:  mainloop.h
fast_pipe.o slow_pipe.o count_pipe.o pipesim.o:  lru_fsm_1way.h lru_fsm_2way.h lru_fsm_3way.h lru_fsm_4way.h

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
