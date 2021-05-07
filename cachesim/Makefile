# Path where things should be installed
R = $(HOME)
B = build

_dummy := $(shell mkdir -p $B)

# Dependent headers
hdrs := opcodes.h insn.h shmfifo.h caveat.h

# Text substitutions
hdrs := $(addprefix $R/include/cava/,$(hdrs))

aobj := tagonlycache.o container.o queues.o utilities.o
cobj := tilesim.o

# Text substitutions
aobj := $(addprefix $B/,$(aobj))
cobj := $(addprefix $B/,$(cobj))

CFLAGS	= -I$R/include/cava -g -Ofast -O0
LIBS	= $R/lib/libcava.a -lrt -lpthread

$B/tilesim:  $(cobj) $R/lib/libcava.a
	$(CC) $(CFLAGS) -o $B/tilesim $^ $(LIBS)

$R/lib/libcava.a:  $(aobj)
	ar rs $@ $^

$B/tilesim.o:  $(hdrs)

$B/%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install: $B/tilesim
	cp $B/tilesim $R/bin/

clean:
	rm -rf $B *~
