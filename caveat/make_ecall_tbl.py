#
#  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
#

import re
import os

symlinks = '../symlinks/'


RVpattern = re.compile(r'#define\s+TARGET_NR_(\S+)\s+(\d+)')
PKpattern = re.compile(r'#define\s+SYS_(\S+)\s+(\d+)')
UNIpattern = re.compile(r'^\s*(\S+)\s+(\d+)')

# Algorith is we make table of RISC-V system call names and record
# their numbers, create a C file of names, include the host x86
# 'asm/unistd_64.h' file to get the correct mapping.

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
            
map = [ (-1, "UNKNOWN") ]*(highest+1)


c = open('tmp.c', 'w')
c.write('#include <asm/unistd_64.h>\n')
for name in rnum.keys():
    c.write('{:s} {:d} __NR_{:s}\n'.format(name, rnum[name], name))
c.close()
os.system('gcc -E tmp.c > tmp.d')

x = open('tmp.d', 'r')
for line in x:
    line = line.rstrip('\r\n')
    if line == '' or line[0] == '#':
        continue
    if re.search('^\s*\d+\s*$', line):
        xn = int(line)
        map[xn] = (int(rn), name)
        continue
    m = UNIpattern.match(line)
    if m:
        (name, rn) = m.groups()
    else:
        print('boo', line)

sf = open('ecall_nums.h', 'w')
sf.write("""
const struct {
    int x64num;
    const char*name;
} rv_to_x64_syscall[] = {
""")

for n in range(0, highest+1):
    (x64num, name) = map[n]
#    print(x64num, name)
    sf.write('    /* {:5d} */ {{ {:5d}, "{:s}" }},\n'.format(n, x64num, name))
sf.write('};\n\n')
sf.write('const int rv_syscall_entries = {:d};\n\n'.format(highest+1))
sf.close()

os.system('rm tmp.[cd]')
