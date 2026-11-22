#!/usr/bin/env python3
"""Compare the register arrays of our assembled Texture2D-equivalent shaders with dborth's. See validate-assembler.sh."""
import re
import sys


def arr(text, name):
    m = re.search(name + r'(?:\[\d*\])?\s*=\s*\{(.*?)\};', text, re.S)
    if not m:
        raise SystemExit('array %s not found' % name)
    return [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]


gen = open(sys.argv[1]).read()
ref = open(sys.argv[2]).read()
ok = True
for ours, theirs in (('kTex2D_VsRegs', 'cpVertexShaderRegs'), ('kTex2D_PsRegs', 'cPixelShaderRegs')):
    a, b = arr(gen, ours), arr(ref, theirs)
    same = a == b
    ok &= same
    print('%s vs %s: %s (%d words)' % (ours, theirs, 'IDENTICAL' if same else 'DIFFERENT', len(a)))
    if not same:
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                print('  word %d: ours %#010x theirs %#010x' % (i, x, y))
vs, vref = arr(gen, 'kTex2D_VsProgram'), arr(ref, 'cpVertexShaderProgram')
cf = vs[:2] == vref[:2]
print('VS CALL_FS control-flow words: %s' % ('IDENTICAL' if cf else 'DIFFERENT %s %s' % (vs[:2], vref[:2])))
ok &= cf
sys.exit(0 if ok else 1)
