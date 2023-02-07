import sys
import os
import re
import json

opcode_line = re.compile('^([ ]|\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+\"(.*)\"\s+(\S+)\s+\"(.*)\"')
reglist_field = re.compile('^(\S)\[(\d+):(\d+)\](\+\d+)?$')

def eprint(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

def diffcp(fname):
    if os.path.exists(fname) and os.system('cmp -s newcode.tmp '+fname) == 0:
        os.system('rm newcode.tmp')
    else:
        os.system('mv newcode.tmp '+fname)

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
                immtyp = 1
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
    while len(reglist) < 4:
        rv.append('NOREG')
    return rv

instructions = {}
for line in sys.stdin:
    m = opcode_line.match(line)
    if not m:
        continue
    (kind, opcode, asm, attr, bits, reglist, action) = m.groups()
    if kind[0] == '#':
        continue
    attr = attr.split(',')
    if '!' in kind:
        attr.append('stop')
    if '+' in kind:
        attr.append('custom')
    opname = 'Op_' + opcode.replace('.', '_')
    reglist = ParseReglist(reglist)
    (code, mask, bytes, immed, immtyp) = ParseOpcode(bits)
    instructions[opcode] = (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action)
    if bytes == 2:
        last_compressed_opcode = opcode

opcodes = ['ZERO'] + [key for key in instructions] + ['ILLEGAL', 'UNKNOWN']

with open('newcode.tmp', 'w') as f:
    json.dump(instructions, f)
diffcp('isa.json')

isa_letter = {}
attribute = {}
for opcode, t in instructions.items():
    (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
    for a in attr:
        if a.isupper():
            isa_letter[a] = 1;
        elif a != '-':
            attribute[a] = 1;

def bitvec(names, typ, uppercase):
    f.write('enum {:s}_t {{'.format(typ))
    sa = 0
    for n in sorted(names):
        if uppercase and not n.isupper() or not uppercase and n.isupper():
            continue
        f.write('\n  {:s}_{:s} = 1<<{:d},'.format(typ, n, sa))
        sa += 1
    if sa < 8:
        bv = 'uint8_t'
    elif sa < 16:
        bv = 'uint16_t'
    elif sa < 32:
        bv = 'uint32_t'
    elif sa < 64:
        bv = 'uint64_t'
    else:
        print('Bitvector too long for enum {:s}_t'.format(typ))
    f.write('\n}};\ntypedef {:s} {:s}_bv_t;\n\n'.format(bv, typ))
    
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
    f.write('const int Number_of_Opcodes = {:d};\n\n'.format(len(opcodes)))
    bitvec(isa_letter.keys(), 'ISA', 1)
    bitvec(attribute.keys(), 'ATTR', 0)
diffcp('../caveat/opcodes.h')

def make_bitvec(typ, tokens, uppercase):
    bv = []
    for t in tokens:
        if uppercase and not t.isupper():  continue
        if not uppercase and t.isupper():  continue
        bv.append('{:s}_{:s}'.format(typ, t))
    if len(bv) == 0:
        return '0'
    else:
        return '|'.join(bv)

with open('newcode.tmp', 'w') as f:
    f.write('const char* op_name[] = {')
    n = 0
    for opcode in opcodes:
        if n % 4 == 0:
            f.write('\n  ')
        f.write('{:20s}'.format('"' + opcode.replace('.','_') + '",'))
        n += 1
    f.write('\n};\n\n')
    f.write('const ISA_bv_t required_isa[] = {')
    n = 0
    for opcode in opcodes:
        if n % 4 == 0:
            f.write('\n  ')
        flags = '0'
        if opcode in instructions:
            flags = make_bitvec('ISA', instructions[opcode][2], 1)
        f.write('{:20s}'.format(flags+','))
        n += 1
    f.write('\n};\n\n')
    f.write('const ATTR_bv_t attributes[] = {\n')
    for opcode in opcodes:
        flags = '0'
        if opcode in instructions:
            flags = make_bitvec('ATTR', instructions[opcode][2], 0)
        f.write('  /* {:19s} */ {:20s}\n'.format(opcode, flags+','))
        n += 1
    f.write('};\n\n')
diffcp('../caveat/constants.h')


with open('newcode.tmp', 'w') as f:
    for opcode, t in instructions.items():
        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
        f.write('  if((b&{:s})=={:s}) {{ i.op_code={:s}; i.op_rd={:s}; i.op_rs1={:s};'.format(mask, code, opname, reglist[0], reglist[1]))
        if immtyp == 1:
            f.write(' i.op_longimm={:s};'.format(immed))
        else:
            f.write(' i.op.rs2={:s}; i.op.rs3={:s}; i.setimm({:s});'.format(reglist[2], reglist[3], immed))
        f.write(' return i; };\n')
diffcp('../caveat/decoder.h')

with open('newcode.tmp', 'w') as f:
    for opcode, t in instructions.items():
        (opname, asm, attr, code, mask, bytes, immed, immtyp, reglist, action) = t
        f.write('    case {:20s} {:s}; pc+={:d}; break;\n'.format(opname+':', action, int(bytes)))
diffcp('../caveat/semantics.h')
    
