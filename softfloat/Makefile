CFLAGS := -I../spike/ -Ofast

src_files := $(wildcard *.c)
obj_files := $(patsubst %.c,%.o,$(src_files))

libsoftfloat.a: $(obj_files)
	ar r $@ $?

clean:
	rm -f *.o

