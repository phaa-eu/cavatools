
# Path where things should be installed
R = /home/peterhsu
B = build

_dummy := $(shell mkdir -p $B)

aobj := tagonlycache.o container.o queues.o utilities.o
cobj := cachesim.o

# Text substitutions
aobj := $(addprefix $B/,$(aobj))
cobj := $(addprefix $B/,$(cobj))

CFLAGS	= -I$R/include/ -g -Ofast
LIBS	= $R/lib/libcava.a -lrt -lpthread

$B/cachesim:  $(cobj) $R/lib/libcava.a
	$(CC) $(CFLAGS) -o $B/cachesim $^ $(LIBS)

$R/lib/libcava.a:  $(aobj)
	ar rs $@ $^

$B/cachesim.o:  $R/include/opcodes.h $R/include/insn.h $R/include/shmfifo.h $R/include/caveat.h

$B/%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install: $B/cachesim
	cp $B/cachesim $R/bin/

clean:
	rm -rf $B *~
