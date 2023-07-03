
import sys
import re

nr_pat = re.compile('#define __NR[^_]*_(\S+)\s+(\d+)')
eq_pat = re.compile('#define __NR[^_]*_(\S+)\s+__NR[^_]*_(\S+)')

equiv = {}
def read_unistd(fname):
    table = {}
    with open(fname, 'r') as f:
        for line in f:
            m = nr_pat.match(line)
            if m:
                name, number = m.groups()
                number = int(number)
                if name in table:
                    print('duplicate', name, 'previously', table[name])
                table[name] = number
                continue
            m = eq_pat.match(line)
            if m:
                name, othername = m.groups()
                equiv[name] = othername
                continue
        for name, othername in equiv.items():
            if othername in table:
                table[name] = table[othername]
            else:
                sys.stderr.write('{:s} no equivalent found in table\n'.format(othername))
    return table

riscv = read_unistd('unistd.h-riscv')
intel = read_unistd('/usr/include/x86_64-linux-gnu/asm/unistd_64.h')

mapping = {}
maxnum = -1
for name, number in riscv.items():
    if number in mapping:
        sys.stderr.write('duplicate mapping {:d} {:s} {:d}:{:s}\n'.format(number, name, mapping[number][0], mapping[number][1]))
        name = mapping[number][1]
    if name in intel:
        mapping[number] = [intel[name], name]
    if number > maxnum:
        maxnum = number

for number in range(maxnum+1):
    if number in mapping:
        hostnum, name = mapping[number]
#        name = '"SYS_' + name + '"'
        name = '"' + name + '"'
    else:
        hostnum, name = -1, '0'
#    print('  /* {:d} */ {{ {:d}, {:s} }},'.format(number, hostnum, name))
    print('  /* {:3d} */ {{ {:3d}, {:s} }},'.format(number, hostnum, name))
print('#define HIGHEST_ECALL_NUM  {:d}'.format(maxnum))
