#
#  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#

import re
import os

PKpattern = re.compile(r'#define\s+SYS_(\S+)\s+(\d+)')
RVpattern = re.compile(r'#define\s+TARGET_NR_(\S+)\s+(\d+)')

# Algorith is we make table of RISC-V system call names and record
# their numbers, create a C file of names, include the host x86
# 'asm/unistd_64.h' file to get the correct mapping.

ecall = {}
enames = {}
highest = -1
rv = open('../include/pk-syscall.h', 'r')
for line in rv:
    m = PKpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
        ecall[num] = name
        enames[name] = num
        highest = max(num, highest)
rv.close()

rv = open('../include/syscall64_nr.h', 'r')
for line in rv:
    m = RVpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
        if num in ecall and name != ecall[num]:
            print('libc {:s} override pk {:s} ecall'.format(name, ecall[num]))
        ecall[num] = name
        enames[name] = num
        highest = max(num, highest)

en = open('ecall_nums.h', 'w')

for name in sorted(enames.keys()):
    en.write('#ifndef __NR_{:s}\n'.format(name))
    en.write('#define __NR_{:s}  -2\n'.format(name))
    en.write('#endif\n')

en.write("""\n
static const struct {
    int sysnum;
    const char*name;
} rv_to_host[] = {
""")

for n in range(0, highest+1):
    if n in ecall:
        name = ecall[n]
        en.write('    /* {:5d} */ {{ __NR_{:s}, "{:s}" }},\n'.format(n, name, name))
    else:
        en.write('    /* {:5d} */ {{ -1, 0 }},\n'.format(n))
    
en.write('};\n\n')
en.write('const int rv_syscall_entries = {:d};\n\n'.format(highest+1))
en.close()
