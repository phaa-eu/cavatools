import sys
import os
import re
import json

repo = sys.argv[1]
if repo[-1] != '/':
    repo += '/'

cava_comment = re.compile('\s*/\*\s*CAVA((?:\s+\w+)+)\s*\*/')

def eprint(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

def diffcp(fname):
    if os.path.exists(fname) and os.system('cmp -s newcode.tmp '+fname) == 0:
        os.system('rm newcode.tmp')
    else:
        os.system('mv newcode.tmp '+fname)

with open('isa.json', 'r') as f:
    instructions = json.load(f)

opcodes = ['ZERO'] + [key for key in instructions] + ['ILLEGAL', 'UNKNOWN']

reg_specifier = re.compile('(\w+)\[(\d+):(\d+)\]')
def gettype(a):
    if a == '-':
        return None
    m = reg_specifier.match(a)
    if not m:
        eprint('Bad register specifier', a)
        exit(-1)
    typ, hi, lo = m.groups()
    if typ == 'bu':  return 'unsigned char'
    if typ == 'hu':  return 'unsigned short'
    if typ == 'wu':  return 'unsigned int'
    if typ == 'b' :  return 'char'
    if typ == 'h' :  return 'short'
    if typ == 'w' :  return 'int'
    if typ == 'l' :  return 'long'
    if typ == 'f' :  return 'float'
    if typ == 'd' :  return 'double'
    if typ == 'a' :  return 'void*'
    eprint('Unknown register type "{:s}"'.format(typ))
    exit(-1)

def gen_asm_header(f):
    for opcode, t in instructions.items():
        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
        if 'custom' not in attr:
            continue

        reglist = reglist.split(',')
        inargs = []
        inputs = []
        asmargs = []
        n = 0
        t = gettype(reglist[0])
        if t == None:
            outtyp = 'void'
            output = '/* no output register */'
        else:
            outtyp = t
            regspec = 'r'
            if t=='float' or t=='double':
                regspec = 'f'
            output = '"={:s}"(y)'.format(regspec)
            asmargs.append('%{:d}'.format(n))
            n += 1
        for k in range(1, 4):
            t = gettype(reglist[k])
            if t:
                inargs.append('{:s} x{:d}'.format('const '+t, n))
                regspec = 'r'
                if t=='float' or t=='double':
                    regspec = 'f'
                inputs.append('"{:s}"(x{:d})'.format(regspec, n))
                asmargs.append('%{:d}'.format(n))
                n += 1
                
        f.write('inline {:s} {:s}({:s}) {{ \n'.format(outtyp, opcode, ', '.join(inargs)))
        if (outtyp != 'void'):
            f.write('  {:s} y; \n'.format(outtyp))
        f.write('  __asm__("{:s}\t{:s}" \n'.format(opcode, ','.join(asmargs)))
        f.write('\t: {:s} \n'.format(output))
        f.write('\t: {:s} \n'.format(','.join(inputs)))
        if 'st' in attr or 'amo' in attr:
            f.write('\t: "memory");\n')
        else:
            f.write('\t: /* no clobber */);\n')
        if (outtyp != 'void'):
            f.write('  return y; \n')
        f.write('};\n\n')


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
                        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
                        if 'custom' not in attr:
                            continue
                        upper_op = opname.upper()
                        f.write('#define MATCH_{:s}  {:s}\n'.format(upper_op, code))
                        f.write('#define MASK_{:s}  {:s}\n'.format(upper_op, mask))
                elif what == 'declare':
                    for opcode, t in instructions.items():
                        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
                        if 'custom' not in attr:
                            continue
                        upper_op = opname.upper()
                        f.write('DECLARE_INSN({:s}, MATCH_{:s}, MASK_{:s})\n'.format(opname, upper_op, upper_op))
                elif what == 'opcode':
                    for opcode, t in instructions.items():
                        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
                        if 'custom' not in attr:
                            continue
                        upper_op = opname.upper()
                        clas = 'I'
                        f.write('{{{:17} {:2d}, INSN_CLASS_{:s}, {:11s} MATCH_{:s}, MASK_{:s}, match_opcode, 0 }},\n'
                                .format('"'+opcode+'",', 0, clas, '"'+asm+'",', upper_op, upper_op)) 
                else:
                    print('Unrecognizable CAVA directive', what)
                    exit(-1)
        f.write(line)
        line = s.readline()
    
    
#repo = '/opt/riscv-gnu-toolchain/'
files = [ repo+'gdb/include/opcode/riscv-opc.h',
          repo+'binutils/include/opcode/riscv-opc.h',
          repo+'gdb/opcodes/riscv-opc.c',
          repo+'binutils/opcodes/riscv-opc.c'
         ]

for n in files:
    with open(n, 'r') as s, open('newcode.tmp', 'w') as f:
        munge_riscv_opc_files(s, f)
    diffcp(n)

with open('newcode.tmp', 'w') as f:
    gen_asm_header(f)
    diffcp('custom_asm_macros.h')
