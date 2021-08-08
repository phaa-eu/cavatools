B := /opt/BUILD/riscv-isa-sim
I :=  -I$B/build -I$B/riscv -I$B/fesvr -I$B/softfloat -I$B/riscv/insns
CXXFLAGS := $I -g -O0

insn_files := $(wildcard *.cc)
obj_files := $(patsubst %.cc,%.o,$(insn_files))

../libcava.a:  $(obj_files)
	ar r ../libcava.a $?
