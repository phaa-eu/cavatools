import sys
import os
import re

opcode_line = re.compile('^([ +])\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+\"(.*)\"\s+(\S+)\s+\"(.*)\"')
reglist_field = re.compile('^(\S)\[(\d+):(\d+)\](\+\d+)?$')

cava_comment = re.compile('\s*/\*\s*CAVA((?:\s+\w+)+)\s*\*/')

def eprint(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

def ParseOpcode(bits):
    (code, mask, pos) = (0, 0, 0)
    immed = []
    immtyp = -1
    for b in reversed(bits.split()):
        if re.match('[01]+', b):
            code |= int(b, 2) << pos
            mask |= ((1<<len(b))-1) << pos
            pos += len(b)
        elif re.match('[.]+', b):
            pos += len(b)
        elif re.match('\{[^}]+\}', b):
            immtyp = 0
            tuple = []
            signed = False
            i = 0
            if b[1] == '-':
                signed = True
                i = 1
            while b[i] != '}':
                i += 1
                m = re.match('(\d+)(:\d+)?', b[i:])
                if not m:
                    eprint('Bad immediate', name, b[i:])
                    exit(-1)
                hi = int(m.group(1))
                lo = hi
                if m.group(2):
                    lo = int(m.group(2)[1:])
                tuple.append((hi, lo))
                i += len(m.group(0))
            for (hi, lo) in reversed(tuple[1:]):
                shift = lo and '<<{:d}'.format(lo) or ''
                immed.append('x({:d},{:d}){:s}'.format(pos, hi-lo+1, shift))
                pos += hi-lo+1
            (hi, lo) = tuple[0]
            if hi >= 13:
                imm = 1
            shift = lo and '<<{:d}'.format(lo) or ''
            immed.append('{:s}({:d},{:d}){:s}'.format(signed and 'xs' or 'x', pos, hi-lo+1, shift))
            pos += hi-lo+1
        else:
            eprint('Unknown field', b, 'in bits "', bits, '"')
            exit(-1)
    if immed:
        immed = '|'.join(reversed(immed))
    else:
        immed = '0'
    if pos == 16:
        digits = 4
    elif pos == 32:
        digits = 8
    else:
        eprint('Illegal length', pos, 'in bits "', bits, '"')
        eprint(immed)
        exit(-1)
    code = '0x' + hex(code)[2:].zfill(digits)
    mask = '0x' + hex(mask)[2:].zfill(digits)
    return (code, mask, pos/8, immed, immtyp)


def ParseReglist(r):
    return None


def diffcp(fname):
    if os.path.exists(fname) and os.system('cmp -s newcode.tmp '+fname) == 0:
        os.system('rm newcode.tmp')
    else:
        os.system('mv newcode.tmp '+fname)

        
instructions = {}
for line in sys.stdin:
    m = opcode_line.match(line)
    if not m: continue
    (kind, opcode, asm, isa, req, bits, reglist, action) = m.groups()
    if kind != '+':
        continue
    opname = 'Op_' + opcode.replace('.', '_')
    reglist = ParseReglist(reglist)
    (code, mask, bytes, immed, immtyp) = ParseOpcode(bits)
    instructions[opcode] = (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action)

opcodes = ['ZERO'] + [key for key in instructions] + ['cas10_w', 'cas10_d', 'cas12_w', 'cas12_d'] + ['ILLEGAL', 'UNKNOWN']


def munge_riscv_opc_files(s, f):
    line = s.readline()
    while line:
        m = cava_comment.match(line)
        if  m:
            directive = m.group(1)
            if 'begin' in directive:
                what = directive.split()[1]
                f.write(line)
                # Skip old stuff between begin and end
                while True:
                    line = s.readline()
                    m = cava_comment.match(line)
                    if m:  break
                # make sure end is same as begin
                if m.group(1).split()[1] != what:
                    print('CAVA end does not match CAVA begin')
                    exit(-1)
                if what == 'define':
                    for opcode, t in instructions.items():
                        (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action) = t
                        upper_op = opname.upper()
                        f.write('#define MATCH_{:s}  {:s}\n'.format(upper_op, code))
                        f.write('#define MASK_{:s}  {:s}\n'.format(upper_op, mask))
                elif what == 'declare':
                    for opcode, t in instructions.items():
                        (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action) = t
                        upper_op = opname.upper()
                        f.write('DECLARE_INSN({:s}, MATCH_{:s}, MASK_{:s})\n'.format(opname, upper_op, upper_op))
                elif what == 'opcode':
                    for opcode, t in instructions.items():
                        (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action) = t
                        upper_op = opname.upper()
                        clas = 'I'
                        f.write('{{{:17} {:2d}, INSN_CLASS_{:s}, {:11s} MATCH_{:s}, MASK_{:s}, match_opcode, 0 }},\n'
                                .format('"'+opcode+'",', 0, clas, '"'+asm+'",', upper_op, upper_op)) 
                else:
                    print('Unrecognizable CAVA directive', what)
                    exit(-1)
        f.write(line)
        line = s.readline()
    
    
repo = '/opt/riscv-gnu-toolchain/'
files = [ repo+'gdb/include/opcode/riscv-opc.h',
          repo+'/binutils/include/opcode/riscv-opc.h',
          repo+'gdb/opcodes/riscv-opc.c',
          repo+'/binutils/opcodes/riscv-opc.c'
         ]

for n in files:
    with open(n, 'r') as s, open('newcode.tmp', 'w') as f:
        munge_riscv_opc_files(s, f)
    diffcp(n)


