# Path where things should be installed
R = $(HOME)
B = build

dummy := $(shell mkdir -p $B)

# Object files for libcava.a
aobj := perfctr.o cache.o
aobj := $(addprefix $B/,$(aobj))

# Object files for caveat not in libcava.a
cobj := pipesim.o mainloop.o
cobj := $(addprefix $B/,$(cobj))

# Include files for libcava.a
incf := perfctr.h cache.h lru_fsm_1way.h lru_fsm_2way.h lru_fsm_3way.h lru_fsm_4way.h

# Libraries
libs := -lrt -lpthread -lm

# Compiler flags
CFLAGS := -I$R/include/cava -g -Ofast
#CFLAGS := -I$R/include/cava -g -O1


# Dependent headers
hdrs := opcodes.h insn.h shmfifo.h caveat.h

# Text substitutions
hdrs := $(addprefix $R/include/cava/,$(hdrs))


# Make targets

all:  	$B/pipesim

install:  $B/pipesim $R/lib/libcava.a $(incf)
	-cp $B/pipesim  $R/bin/pipesim
	-cp $(incf)  $R/include/cava

.PHONY:  clean

$R/lib/libcava.a:  $(aobj)
	ar rcs $@ $^


clean:
	rm -f lru_fsm_?way.h *.o *~ ./#*#
	rm -rf build


# Pipeline simulator and library

$B/pipesim:  $(cobj) $R/lib/libcava.a
	$(CC) $(CFLAGS) -o $@ $^ $(libs)


# Dependencies

$B/pipesim.o $B/simulate.o:  pipesim.h  cache.h perfctr.h $(hdrs) $(incf)
$B/perfctr.o:  perfctr.h

lru_fsm_1way.h:  make_cache
	./make_cache 1

lru_fsm_2way.h:  make_cache
	./make_cache 2

lru_fsm_3way.h:  make_cache
	./make_cache 3

lru_fsm_4way.h:  make_cache
	./make_cache 4

$B/%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<
