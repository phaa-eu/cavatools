
# Path where things should be installed
R = /home/peterhsu

OBJS = tagonlycache.o container.o queues.o utilities.o cachesim.o

CFLAGS	= -I$R/include/ -g -Ofast
LIBS	= $R/lib/libcava.a -lrt -lpthread

cachesim:  $(OBJS)
	$(CC) $(CFLAGS) -o cachesim $^ $(LIBS)

cachesim.o:  $R/include/opcodes.h $R/include/insn.h $R/include/shmfifo.h $R/include/caveat.h

install: cachesim
	cp cachesim $R/bin/

clean:
	rm -f cachesim *.o *~
