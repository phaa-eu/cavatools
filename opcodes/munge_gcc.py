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


def gen_asm_header(f):
    for opcode, t in instructions.items():
        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
        if 'custom' not in attr:
            continue
        outreg = '/* no output registers */'
        inlist = []
        macro_operands = []
        asm_operands = []
        aop = 0
        print(opcode, reglist)
        for k in range(len(reglist)):
            if reglist[k] != 'NOREG' and not reglist[k].isnumeric():
                if 'FPREG' in reglist[k]:
                    letter = 'f'
                else:
                    letter = 'r'
                if k == 0:
                    outreg = '"={:s}"(xd)'.format(letter)
                    macro_operands.append('xd')
                else:
                    inlist.append('"{:s}"(x{:d})'.format(letter, k))
                    macro_operands.append('x{:d}'.format(k))
                asm_operands.append('%{:d}'.format(aop))
                aop += 1
        if len(inlist) > 0:
            inlist = ','.join(inlist)
        else:
            inlist = '/* no input registers */'
        f.write('#define {:s}({:s}) __asm__( \\\n'.format(opcode, ','.join(macro_operands)))
        f.write('\t\"{:s}\t{:s}" \\\n'.format(opcode, ','.join(asm_operands)))
        f.write('\t: {:s} \\\n'.format(outreg))
        f.write('\t: {:s} \\\n'.format(inlist))
        if 'st' in attr or 'amo' in attr:
            f.write('\t: "memory" \\\n')
        else:
            f.write('\t: /* no clobber */ \\\n')
        f.write(');\n')


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
