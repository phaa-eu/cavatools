
# Path where things should be installed
R = /home/peterhsu
B = build

_dummy := $(shell mkdir -p $B)

# Dependent headers
hdrs := opcodes.h insn.h shmfifo.h caveat.h

# Text substitutions
hdrs := $(addprefix $R/include/cava/,$(hdrs))

aobj := tagonlycache.o container.o queues.o utilities.o
cobj := cachesim.o

# Text substitutions
aobj := $(addprefix $B/,$(aobj))
cobj := $(addprefix $B/,$(cobj))

CFLAGS	= -I$R/include/cava -g -Ofast
LIBS	= $R/lib/libcava.a -lrt -lpthread

$B/cachesim:  $(cobj) $R/lib/libcava.a
	$(CC) $(CFLAGS) -o $B/cachesim $^ $(LIBS)

$R/lib/libcava.a:  $(aobj)
	ar rs $@ $^

$B/cachesim.o:  $(hdrs)

$B/%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install: $B/cachesim
	cp $B/cachesim $R/bin/

clean:
	rm -rf $B *~
