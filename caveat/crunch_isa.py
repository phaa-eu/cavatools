import re
BinaryPattern = re.compile(r'([01]+)')
FieldPattern = re.compile(r'([a-zA-Z][a-zA-Z0-9]*)\[([0-9]+)\]')
ImmedPattern = re.compile(r'\{(\-?[0-9:|]+)\}')
RangePattern = re.compile(r'\|?([0-9]+):([0-9]+)')
SinglePattern = re.compile(r'\|?([0-9]+)')
ParamPattern = re.compile(r'([a-zA-Z][a-zA-Z0-9_]*)(.*)')

InsnFile = open('Instructions.def', 'r')
df = open('opcodes.h', 'w')
rf = open('decode_insn.h', 'w')
ef = open('execute_insn.h', 'w')
af = open('disasm_insn.h', 'w')
kf = open('opcodes_attr.h', 'w')

Field = {}
Opcode = {}
Mnemonic = {}
OriginalOrder = [ 'Op_zero' ]

for line in InsnFile:
    CodeBits = []
    Param = {}
    Immed = []
    signed = False
    line = line.rstrip('\r\n')
    # print(line)
    if line == "" or line[0] != "@":
        continue
    line = line[1:]
    tuples = re.split('\t+', line)
    if len(tuples) < 5:
        print(line)
        print('Bad Line')
        exit(-1)
    (bitpattern, mnemonic, assembly, regspecs, action) = tuples
    # Create opcode bitmask
    tuples = bitpattern.split()
    InsnLen = 0
    for token in tuples:
        token.strip()
        m = BinaryPattern.match(token)
        if m:
            bits, = m.groups()
            InsnLen += m.end()
            CodeBits.append([InsnLen, bits])
            continue
        m = FieldPattern.match(token)
        if m:
            name, width = m.groups()
            width = int(width)
            InsnLen += width
            Param[name] = [InsnLen, width]
            continue
        m = ImmedPattern.match(token)
        if m:
            bits, = m.groups()
            if  bits[0] == '-':
                signed = True
                bits = bits[1:]
            while (bits != ""):
                m = RangePattern.match(bits)
                if m:
                    high, low = m.groups()
                    high = int(high)
                    low = int(low)
                    bits = bits[m.end():]
                    InsnLen += high-low+1
                    Immed.append([InsnLen, high, low])
#                    print "Range", high, low, bits
                    continue
                m = SinglePattern.match(bits)
                if m:
                    where, = m.groups()
                    where = int(where)
                    bits = bits[m.end():]
                    InsnLen += 1
                    Immed.append([InsnLen, where, where])
#                    print "Single", where, bits
            continue
        print()
        print('Bad Token')
    if not (InsnLen == 16 or InsnLen == 32):
        print(line)
        print('Illegal instruction length', InsnLen)
        exit(-1)

    code = 0
    mask = 0
    for pos, bits in CodeBits:
        pos = InsnLen - pos
        code |= int(bits, 2) << pos
        mask |= (2**len(bits)-1) << pos

    for f in Param:
        Param[f][0] = InsnLen - Param[f][0]
        if f in Field:
            if  Param[f][0] != Field[f][0] or Param[f][1] != Field[f][1]:
                print(line)
                print('Redefinition of field', f)
                exit(-1)
        else:
            Field[f] = Param[f]

    for i in range(0, len(Immed)):
        Immed[i][0] = InsnLen - Immed[i][0]
    mnemonic = mnemonic.strip()
    mnemonic = mnemonic.lower()
    op = 'Op_' + mnemonic.replace('.', '_')
    OriginalOrder.append(op)
    Opcode[op] = [ code, mask, signed, int(InsnLen/8), regspecs.strip(), Immed, action.strip(), assembly.strip() ]
    Mnemonic[op] = mnemonic
InsnFile.close()

OriginalOrder.append('Op_illegal')


Opcode['Op_zero'   ] = (0, 0, False, 0, 'i,-,-', 0, '', 'UNKNOWN')
Opcode['Op_illegal'] = (0, 0, False, 0, 'i,-,-', 0, '', 'ILLEGAL')
Mnemonic['Op_zero'   ] = 'ZERO'
Mnemonic['Op_illegal'] = 'ILLEGAL'

for op in OriginalOrder:
    code, mask, signed, len, regspecs, Immed, action, assembly = Opcode[op]
    tokens = re.split('[,()]', assembly)
    format = assembly
    params = ''
    have_immed = None
    while tokens:
        t = tokens.pop(0)
        if t == '':
            break
        if t == 'immed' or t == 'constant':
            format = format.replace(t, '%d')
            if t == 'immed':
                have_immed = ', p->op.immed'
            else:
                have_immed = ', p->op_constant'
            params += have_immed
        elif t[0] == 'r' or t[0] == 'f':
            format = format.replace(t, '%s')
            if t[0] == 'f':
                t = t.replace('f', 'r')
            regs = 'regName'
            t = t.replace('rd', 'p->op_rd')
            t = t.replace('rs1', 'p->op_rs1')
            t = t.replace('rs2', 'p->op.rs2')
            t = t.replace('rs3', 'p->op.rs3')
            t = t.replace('immed', 'p->op.immed')
            t = t.replace('constant', 'p->op_constant')
            params += ', '+regs+'['+t+']'
    if have_immed:
        format += ' [0x%x]'
        params += have_immed
    af.write('        case {:s}: n += sprintf(buf, \"{:s}\"{:s}); break;\n'.format(op, format, params))


def ExpandField(x):
    mo = ParamPattern.match(x)
    if not mo:
        return x
    param, expr = mo.groups()
    if not param in Field:
        return x
    pos, width = Field[param]
#    extract = '((ir>>{:d})&0x{:x})'.format(32-pos-width, 32-width)
    extract = '((ir>>{:d})&0x{:x})'.format(pos, (1<<width)-1)
    return extract + expr

Flags = {}
ShortOp = []
LongOp = []
KonstOp = {}
ReadOp = {}
WriteOp = {}
ThreeOp = {}
for op in OriginalOrder:
    if op == 'Op_zero' or op == 'Op_illegal':
        continue
    code, mask, signed, len, regspecs, Immed, action, assembly = Opcode[op]
    rf.write('    if ((ir&0x{:08x})==0x{:08x}) {{ '.format(mask, code))

    reglist = regspecs.split(',');
    flagspecs = reglist[0].split(',')[0]
    for f in list(flagspecs):
        Flags[f] = 1
#        if f=='r':
#            ReadOp[op] = 1
#        if f=='w':
#            WriteOp[op] = 1
    if 'm' in flagspecs:
        if 'l' in flagspecs:
            ReadOp[op] = 1
        if 's' in flagspecs:
            WriteOp[op] = 1
    reg = [ '64', '64', '64', '64' ]
    imm = '0'
    for i, spec in enumerate(reglist[1:]):
        if spec == '-':
            continue
        spec = spec.replace('cfd+8', 'crd+8+32')
        spec = spec.replace('ds2+8', 'cs2+8+32')
        spec = spec.replace('cfs2+8', 'crs2+8+32')
        spec = spec.replace('fd', 'rd+32')
        spec = spec.replace('fs1', 'rs1+32')
        spec = spec.replace('fs2', 'rs2+32')
        spec = spec.replace('fs3', 'fs3+32')
        reg[i] = ExpandField(spec)
        if spec.find('s3') != -1:
            ThreeOp[op] = 1
    if Immed:
        if signed:
            (pos, hi, lo) = Immed.pop(0)
            width = hi-lo+1
            imm += '|(((ir<<{:d})>>{:d})<<{:d})'.format(32-pos-width, 32-width, lo)
        for (pos, hi, lo) in Immed:
            width = hi-lo+1
            mask = (2**width)-1
            imm += '|(((ir>>{:d})&0x{:x})<<{:d})'.format(pos, mask, lo)
    if assembly.find('constant') != -1:
        KonstOp[op] = 1
        rf.write("*p=fmtC({:s}, {:s}, {:s}, {:s})".format(op, reg[0], reg[1], imm))
    else:
        rf.write("*p=fmtR({:s}, {:s}, {:s}, {:s}, {:s}, {:s})".format(op, reg[0], reg[1], reg[2], reg[3], imm))
    if (Opcode[op][3] == 2):
        ShortOp.append(op)
    else:
        LongOp.append(op)
    Opcode[op][4] = regspecs;
    rf.write("; return; }\n")

    

InOrder = [ 'Op_zero' ]

# 1 short ops with long constants (cannot be memory)
for op in ShortOp:
    if op in KonstOp and op not in ReadOp and op not in WriteOp:
        if op in ReadOp or op in WriteOp:
            print("Short op ", op, "in KonstOp and MemOp!")
            exit(1)
        InOrder.append(op)
        lastShortKonstOp = op
# 2 short ops not memory read or write operations
for op in ShortOp:
    if op not in ReadOp and op not in WriteOp and op not in KonstOp:
        InOrder.append(op)
# 3 short ops that are memory read operations
firstShortMemOp = None
for op in ShortOp:
    if op in ReadOp and op not in WriteOp and op not in KonstOp:
        InOrder.append(op)
        if firstShortMemOp == None:
            firstShortMemOp = op
# 4 short ops that are memory write or read-modify-write operations
firstWriteOp = None
for op in ShortOp:
    if op in WriteOp and op not in KonstOp:
        InOrder.append(op)
        lastShortOp = op
        if firstWriteOp == None:
            firstWriteOp = op

# 5 long ops that are memory write or read-modify-write operations
for op in LongOp:
    if op in WriteOp and op not in KonstOp:
        InOrder.append(op)
        lastWriteOp = op

# 6 long ops that are memory read operations
for op in LongOp:
    if op in ReadOp and op not in WriteOp and op not in KonstOp:
        InOrder.append(op)
        lastLongMemOp = op
        
# 7 long ops without long constants that do not have three operands
for op in LongOp:
    if op not in ThreeOp and op not in ReadOp and op not in WriteOp and op not in KonstOp:
        InOrder.append(op)
        
# 8 long ops without long constants that do have three operands
firstThreeOp = None
for op in LongOp:
    if op in ThreeOp and op not in ReadOp and op not in WriteOp and op not in KonstOp:
        InOrder.append(op)
        if firstThreeOp == None:
            firstThreeOp = op
            
# 9 long ops with long constants
firstLongKonstOp = None
for op in LongOp:
    if op in KonstOp and op not in ReadOp and op not in WriteOp:
        InOrder.append(op)
        if firstLongKonstOp == None:
            firstLongKonstOp = op
            
InOrder.append('Op_illegal')


df.write('enum Opcode_t {')
j = 0
for op in InOrder:
    if j % 4 == 0:
        df.write('\n  ')
    df.write('{:>20s},'.format(op))
    j += 1
df.write('{:>20s},'.format('Number_of_opcodes'))
df.write('\n};\n\n')

df.write('#define validOp(op)    (Op_zero < op && op < Op_illegal)\n')
df.write('#define shortOp(op)  (op <= {:s})\n'.format(lastShortOp))
df.write('#define konstOp(op)  (op <= {:s} || op >= {:s})\n'.format(lastShortKonstOp, firstLongKonstOp))
df.write('#define memOp(op)    ({:s} <= op && op <= {:s})\n'.format(firstShortMemOp, lastLongMemOp))
df.write('#define writeOp(op)  ({:s} <= op && op <= {:s})\n'.format(firstWriteOp, lastWriteOp))
df.write('#define threeOp(op)  (op >= {:s})\n'.format(firstThreeOp))
df.write('\n\n')


val = 1
for f in sorted(Flags):
    df.write('#define attr_{:s}  0x{:08x}\n'.format(f, val))
    val = val << 1
    
# write opcodes_attr.h
for i, op in enumerate(InOrder):
    init = '{:16s}  0'.format('"'+Mnemonic[op]+'",')
    flags = Opcode[op][4]
    flags = flags.split(',')
    flags = flags[0]
    for letter in flags:
        init += ' | attr_' + letter
    kf.write('    {{ {:s} }},\n'.format(init))
    

for op in InOrder:
    if op == 'Op_zero' or op == 'Op_illegal':
        continue
    code, mask, signed, len, regspecs, Immed, action, assembly = Opcode[op]
    action = action.replace('rd', 'p->op_rd')
    action = action.replace('rs1', 'p->op_rs1')
    action = action.replace('rs2', 'p->op.rs2')
    action = action.replace('rs3', 'p->op.rs3')
    action = action.replace('immed', 'p->op.immed')
    action = action.replace('constant', 'p->op_constant')
    ef.write('case {:>20s}: {:s};  INCPC({:d});  break;\n'.format(op, action, len))

af.close()
df.close()
rf.close()
ef.close()
