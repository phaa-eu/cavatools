RVTOOLS := /opt/riscv-tools
B := $(RVTOOLS)/riscv-isa-sim
L := $B/build/libriscv.a $B/build/libsoftfloat.a $B/build/libdisasm.a

#CXXFLAGS := -g -Ofast
CXXFLAGS := -g -O0
LDFLAGS := -Wl,-Ttext=70000000

install:  caveat
	cp caveat ~/bin/.

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f lru_fsm_?way.h
	rm -f caveat


caveat:  simulator.o cache.o ../uspike/libcava.a
	g++ -o caveat $^ $(LDFLAGS) $L -ldl

cache.o simulator.o:  cache.h

simulator.o: lru_fsm_1way.h lru_fsm_2way.h lru_fsm_3way.h lru_fsm_4way.h

lru_fsm_1way.h: make_cache
	./make_cache 1

lru_fsm_2way.h: make_cache
	./make_cache 2

lru_fsm_3way.h: make_cache
	./make_cache 3

lru_fsm_4way.h: make_cache
	./make_cache 4


#model.h: CAVA.model parse_model
#	./parse_model ../uspike/RV_isa.tmp CAVA.model

