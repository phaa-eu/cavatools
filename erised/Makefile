# Path where things should be installed
R = $(HOME)

//CFLAGS   = -I$R/include/cava -g -O3
CFLAGS   = -I$R/include/cava -g -O0
LIBS     = $R/lib/libcava.a -lrt -lpthread -lncurses
LDFLAGS  = -Wl,-Ttext=70000000

# Dependent headers
hdrs := caveat.h opcodes.h insn.h

# Text substitutions
hdrs := $(addprefix $R/include/cava/,$(hdrs))


erised:  erised.o $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

erised.o:  $(hdrs) ../pipesim/pipesim.h

install:  erised
	-cp $^ $R/bin/

.PHONY:
clean:
	rm -f erised *.o *~ ./#*#



