import re
#import subprocess
import os
import sys

cjump = [ 'beq', 'bgeu', 'blt', 'bltu', 'bne', 'beqz', 'bneq', 'c.beqz', 'c.bnez' ]
ujump = [ 'c.jalr', 'c.j', 'c.jr', 'dret', 'jal', 'jalr' ]

filepat = re.compile(r'(\w+)\.h')
matchmaskpat = re.compile(r'#define\s+(MATCH|MASK)_(\S+)\s+(\S+)')

def diffcp(fname):
    if os.path.exists(fname) and os.system('cmp -s newcode.tmp '+fname) == 0:
        os.system('rm newcode.tmp')
    else:
        os.system('mv newcode.tmp '+fname)
        
body = {}
for op in sys.argv[1:]:
    if op=='ecall' or op=='c_ebreak' or op=='ebreak':
        continue
    if 'amo' in op:
        continue
    if '_q' in op:
        continue
    with open('golden/'+op+'.h') as f:
        data = f.read()
        if 'require_privilege' not in data and 'require_rv32' not in data:
            body[op] = data
    
compressed = []
uncompressed = []
for op in body.keys():
    if op[0:2] == 'c_':
        compressed.append(op)
    else:
        uncompressed.append(op)

match = {}
valid = {}
with open('encoding.h') as f:
    for line in f:
        m = matchmaskpat.match(line)
        if m:
            key, op, value = m.groups()
            op = op.lower()
            if op not in body:
                continue
            value = int(value, 16)
            if key == 'MATCH':
                match[op] = value
            else:
                valid[op] = value


# create spike.def file

def binary(n, w):
    digits = []
    while w > 0:
        digits = [str(n % 2)] + digits
        n //= 2
        w -= 1
    return digits
    
        
with open('newcode.tmp', 'w') as f:
    for op in compressed + uncompressed:
        name = op.replace('_', '.')
        attr = []
        if op[0:2] == 'c_':
            attr.append('C')
            width = 16
        else:
            attr.append('I')
            width = 32
        if name in ujump:
            attr.append('uj')
        if name in cjump:
            attr.append('cj')
        if name in ujump+cjump:
            attr.append('>')
        
        code = binary(match[op], width)
        mask = binary(valid[op], width)
        for k in range(width):
            if mask[k] == '0':
                code[k] = '.'
        bits = ''
        lastb = 'b'
        for b in code:
            if (b == '.' and lastb != '.') or (b != '.' and lastb == '.'):
                bits += ' '
            bits += b
            lastb = b
        if bits[0] == ' ':
            bits = bits[1:]
        bits = '"' + bits + '"'
        
        call = 'I_{:s}(p, pc)'.format(op)
        attr = ','.join(attr)
        if attr == '':
            attr = '-'
        f.write('!  {:20s}\t{:15s}\t{:15s}\t{:>47s}\t-,-,-,-\t\t\t\t\t"pc = {:s}"\n'.format(name, '-', attr, bits, call))
diffcp('../opcodes/spike.def')

# create function declarations

with open('newcode.tmp', 'w') as f:
    for op in compressed+uncompressed:
        f.write('reg_t I_{:s}(processor_t* p, reg_t pc);\n'.format(op))
diffcp('../caveat/spike_insns.h')

# create interpretation functions

for op in compressed+uncompressed:
    with open('newcode.tmp', 'w') as f:
        bytes = 4
        if op[0:2] == 'c_':
            bytes = 2
        f.write('''
#include "spike_link.h"
reg_t I_{:s}(processor_t* p, reg_t pc)
{{
  insn_t insn = (long)(*(int{:d}_t*)pc);
  long npc = pc + {:d};
{:s}
  return npc;
}} 
'''.format(op, 8*bytes, bytes, body[op]))

    diffcp('build/' + op +'.cc')


        
