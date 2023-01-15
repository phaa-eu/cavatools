import sys
import os
import re

opcode_line = re.compile('^\s*(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+\"(.*)\"\s+(\S+)\s+\"(.*)\"')
reglist_field = re.compile('^(\S)\[(\d+):(\d+)\](\+\d+)?$')

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


def ParseReglist(reglist):
    rv = []
    for r in reglist.split(','):
        if r == '-':
            rv.append('NOREG')
            continue
        elif r[0].isnumeric():
            rv.append(r)
            continue
        m = reglist_field.match(r)
        if not m:
            eprint('Illegal register specifier', r, 'in', reglist)
            exit(-1)
        (typ, hi, lo, plus) = m.groups()
        if not plus:
            plus = ''
        if   typ == 'x':  typ = '+GPREG'
        elif typ == 'f':  typ = '+FPREG'
        else:
            eprint('unknown type of register', typ, 'in', reglist)
            exit(-1)
        rv.append('x({:d},{:d}){:s}{:s}'.format(int(lo), int(hi)-int(lo)+1, plus, typ))
    return rv

def diffcp(fname):
    if os.path.exists(fname) and os.system('cmp -s newcode.tmp '+fname) == 0:
        os.system('rm newcode.tmp')
    else:
        os.system('mv newcode.tmp '+fname)

instructions = {}
for line in sys.stdin:
    m = opcode_line.match(line)
    if not m: continue
    (opcode, asm, isa, req, bits, reglist, action) = m.groups()
    opname = 'Op_' + opcode.replace('.', '_')
    reglist = ParseReglist(reglist)
    (code, mask, bytes, immed, immtyp) = ParseOpcode(bits)
    instructions[opcode] = (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action)
    if bytes == 2:
        last_compressed_opcode = opcode

opcodes = ['ZERO'] + [key for key in instructions] + ['cas10_w', 'cas10_d', 'cas12_w', 'cas12_d'] + ['ILLEGAL', 'UNKNOWN']


with open('newcode.tmp', 'w') as f:
    f.write('enum Opcode_t : short {')
    n = 0
    for opcode in opcodes:
        if n % 4 == 0:
            f.write('\n  ')
        f.write('{:20s}'.format('Op_' + opcode.replace('.','_') + ','))
        n += 1
    f.write('\n};\n\n')
    f.write('const Opcode_t Last_Compressed_Opcode = Op_{:s};\n'.format(last_compressed_opcode.replace('.','_')))
diffcp('../uspike/opcodes.h')


with open('newcode.tmp', 'w') as f:
    f.write('const char* op_name[] = {')
    n = 0
    for opcode in opcodes:
        if n % 4 == 0:
            f.write('\n  ')
        f.write('{:20s}'.format('"' + opcode.replace('.','_') + '",'))
        n += 1
    f.write('\n};\n')
diffcp('../uspike/constants.h')


with open('newcode.tmp', 'w') as f:
    for opcode, t in instructions.items():
        (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action) = t
        f.write('  if((b&{:s})=={:s}) return '.format(mask, code))
        if len(reglist) == 4:
            f.write('Insn_t({:s}, {:s}, {:s}, {:s}, {:s}, {:s})'.format(opname, reglist[0], reglist[1], reglist[2], reglist[3], immed))
        elif len(reglist) == 3:
            f.write('Insn_t({:s}, {:s}, {:s}, {:s}, {:s})'.format(opname, reglist[0], reglist[1], reglist[2], immed))
        elif len(reglist) == 2:
            f.write('Insn_t({:s}, {:s}, {:s}, {:s})'.format(opname, reglist[0], reglist[1], immed))
        elif len(reglist) == 1:
            f.write('Insn_t({:s}, {:s}, {:s}, {:s})'.format(opname, reglist[0], immed))
        elif len(reglist) == 0:
            f.write('Insn_t({:s}, {:s}, {:s}, {:s})'.format(opname, 'NOREG', immed))
        else:
            eprint('reglist length not 0-4')
            exit(-1)
        f.write(';\n')
diffcp('../uspike/decoder.h')

with open('newcode.tmp', 'w') as f:
    for opcode, t in instructions.items():
        (opname, asm, isa, req, code, mask, bytes, immed, immtyp, reglist, action) = t
        f.write('long I_{:s}(long pc, mmu_t& MMU, class processor_t* cpu);\n'.format(opcode.replace('.', '_')))
    f.write('\n')
    
    f.write('long (*golden[])(long pc, mmu_t&MMU, class processor_t* cpu) = {')
    n = 0
    for opcode in opcodes:
        if n % 4 == 0:
            f.write('\n  ')
        if opcode in instructions:
            f.write('{:20s}'.format('&I_' + opcode.replace('.','_')+','))
        else:
            f.write('{:20s}'.format('0,'))
        n += 1
    f.write('\n};\n')
diffcp('../uspike/dispatch_table.h')
    
