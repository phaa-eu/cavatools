
include Makefile.insns

riscv_insn_list = \
	$(riscv_insn_ext_a) \
	$(riscv_insn_ext_c) \
	$(riscv_insn_ext_i) \
	$(riscv_insn_ext_m) \
	$(riscv_insn_ext_f) \
	$(riscv_insn_ext_d) \

#	$(riscv_insn_ext_q) \
	$(riscv_insn_ext_zfh) \
	$(riscv_insn_ext_b) \
        $(riscv_insn_ext_k) \
	$(riscv_insn_ext_v) \
	$(riscv_insn_ext_h) \
	$(riscv_insn_ext_p) \

insn_files := $(wildcard build/*.cc)
obj_files := $(patsubst build/%.cc,%.o,$(insn_files))

# Compiling options
CXXFLAGS := -g -O3 -I. -I../softfloat

install: ../opcode/spike.def ../caveat/spike_insns.h build/libspike.a processor.o

build/libspike.a ../opcode/spike.def ../caveat/spike_insns.h:
	python make_insns.py $(riscv_insn_list)
	make -j -C build

clean:
	rm -f *.o *~ ./#*# *.tmp
	rm -f ../opcode/spike.def
	make -C build clean
