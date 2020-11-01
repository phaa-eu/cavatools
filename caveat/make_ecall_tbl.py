#
#  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#

import re

symlinks = '../symlinks/'


NRpattern = re.compile(r'#define\s+TARGET_NR_(\S+)\s+(\d+)')
SYSpattern = re.compile(r'#define\s+SYS_(\S+)\s+(\d+)')

rnum = {}
highest = -1
rfile = open(symlinks+'riscv-gnu-toolchain/qemu/linux-user/riscv/syscall_nr.h', 'r')
for line in rfile:
    m = NRpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
        rnum[name] = num
        highest = max(num, highest)

pfile = open(symlinks+'riscv-pk/pk/syscall.h', 'r')
for line in pfile:
    m = SYSpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
        if name in rnum:
            if num != rnum[name]:
                print('Mismatch RV syscall numbers', name, rnum[name], num)
        else:
            rnum[name] = num
            highest = max(num, highest)

xnum = {}
xfile = open(symlinks+'riscv-gnu-toolchain/qemu/linux-user/x86_64/syscall_nr.h', 'r')
for line in xfile:
    m = NRpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
        xnum[name] = num

map = [ None ]*(highest+1)
for name in rnum.keys():
    if name in xnum:
        map[ rnum[name] ] = ( xnum[name], name )

sf = open('ecall_nums.h', 'w')
sf.write("""
const struct {
    int x64num;
    const char*name;
} rv_to_x64_syscall[] = {
""")

for n in range(0, highest+1):
    if map[n] == None:
        x64num, name = -1, "UNKNOWN"
    else:
        x64num, name = map[n]
    sf.write('    /* {:5d} */ {{ {:5d}, "{:s}" }},\n'.format(n, x64num, name))
sf.write('};\n\n')
sf.write('const int rv_syscall_entries = {:d};\n\n'.format(highest+1))
sf.close()
