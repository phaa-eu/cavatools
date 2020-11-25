# Path where things should be installed
R = $(HOME)

CFLAGS   = -I$R/include/cava -g -O3
LIBS     = $R/lib/libcava.a -lrt -lpthread
LDFLAGS  = -Wl,-Ttext=70000000

# Dependent headers
hdrs := opcodes.h insn.h shmfifo.h caveat.h

# Text substitutions
hdrs := $(addprefix $R/include/cava/,$(hdrs))


traceinfo:  traceinfo.o $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

traceinfo.o:  $(hdrs)

install:  traceinfo
	-cp $^ $R/bin/

.PHONY:
clean:
	rm -f traceinfo pipesim cache_?way.h *.o *~ ./#*#
