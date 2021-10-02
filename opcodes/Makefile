#
#  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#
ifndef RVTOOLS
RVTOOLS := /opt/riscv-tools
endif
ifndef CAVA
CAVA := $(HOME)
endif

# Collect all the opcodes
RVOPS := $(RVTOOLS)/riscv-opcodes
RVINS := $(RVTOOLS)/riscv-isa-sim/riscv/insns

RV32 := rv32i rv32m rv32a rv32f rv32d system
ISAg := $(RV32) rv64i rv64m rv64a rv64f rv64d
ISAv := rvv
ISAp := rv32b rv32c rv32d-zfh rv32h rv32k rv64b rv64c rv64h rv64k rvk

ISAg := $(addprefix $(RVOPS)/opcodes-, $(ISAg))
ISAv := $(addprefix $(RVOPS)/opcodes-, $(ISAv))
ISAp := $(addprefix $(RVOPS)/opcodes-, $(ISAp))

all:  ISA.json

ISA.json SPIKE.json &: ./crunch_isa FP.bits RV_isa.tmp RV.fast
	./crunch_isa FP.bits RV_isa.tmp RV.fast
#	./crunch_isa RV_isa.tmp RV.fast
#	./crunch_isa RV_isa.tmp

RV_isa.tmp: parse_spike RV_bits.tmp RVC.bits
	./parse_spike $(RVINS) RV_bits.tmp RVC.bits > $@

RV_bits.tmp: parse_opcodes
	./parse_opcodes $(ISAg) > $@
#	./parse_opcodes $(ISAg) $(ISAv) $(ISAp) > $@

# Cleanup & installation

clean:
	rm -f *.o *~ ./#*# *.tmp

install:  all
