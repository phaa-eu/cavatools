#
#  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#

import re

symlinks = '../symlinks/'


RVpattern = re.compile(r'#define\s+TARGET_NR_(\S+)\s+(\d+)')
PKpattern = re.compile(r'#define\s+SYS_(\S+)\s+(\d+)')
X86pattern = re.compile(r'#define\s+__NR_(\S+)\s+(\d+)')

rnum = {}
highest = -1
rfile = open('../include/syscall64_nr.h', 'r')
for line in rfile:
    m = RVpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
#        print("RV name={:s}, num={:d}".format(name, num))
        rnum[name] = num
        highest = max(num, highest)

pfile = open('../include/pk-syscall.h', 'r')
for line in pfile:
    m = PKpattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
#        print("PK name={:s}, num={:d}".format(name, num))
        if name in rnum:
            if num != rnum[name]:
                print('Mismatch RV syscall numbers', name, rnum[name], num)
        else:
            rnum[name] = num
            highest = max(num, highest)

xnum = {}
xfile = open('/usr/include/x86_64-linux-gnu/asm/unistd_64.h', 'r')
for line in xfile:
    m = X86pattern.match(line)
    if m:
        name, num = m.groups()
        num = int(num)
#        print("X86 name={:s}, num={:d}".format(name, num))
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
