

# Cavatools installed in $(CAVA)/bin, $(CAVA)/lib, $(CAVA)/include/cava
ifndef CAVA
CAVA := $(HOME)
endif

CFLAGS := -I../spike/ -Ofast

src_files := $(wildcard *.c)
obj_files := $(patsubst %.c,%.o,$(src_files))

libsoftfloat.a: $(obj_files)
	ar r $@ $?

install:  libsoftfloat.a
	cp libsoftfloat.a $(CAVA)/lib/
	mkdir -p $(CAVA)/include/softfloat
	cp *.h $(CAVA)/include/softfloat

clean:
	rm -f *.o

