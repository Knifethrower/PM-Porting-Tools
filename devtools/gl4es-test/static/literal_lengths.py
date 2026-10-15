#!/usr/bin/env python3
"""Static check of gl4es sources: calls that pass a string literal together with its length by hand
(APPEND_OUTPUT("sin(", 9) in arbgenerator.c copied 9 bytes of a 4-byte string: every ARB SIN became
broken GLSL). Prints every call whose number differs from the literal's length.
usage: literal_lengths.py <gl4es src dir>"""
import os, re, sys

CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\((?:\s*[^,"()]+(?:\([^()]*\))?\s*,)?\s*((?:"(?:[^"\\]|\\.)*"\s*)+),\s*(\d+)\s*\)')

def c_len(lits):
    s = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', lits))
    return len(bytes(s, 'utf-8').decode('unicode_escape'))

def main(root):
    bad = 0
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith(('.c', '.h')):
                continue
            p = os.path.join(dp, f)
            for no, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
                for m in CALL.finditer(line):
                    fn, lit, n = m.group(1), m.group(2), int(m.group(3))
                    # only calls whose number is a byte count
                    if not re.search(r'APPEND|append|cpy|cmp|memc|Append', fn):
                        continue
                    L = c_len(lit)
                    if L != n:
                        bad += 1
                        print(f'{os.path.relpath(p, root)}:{no}: {fn}({lit.strip()}, {n})  literal is {L} bytes')
    print(f'{bad} mismatches')

if __name__ == '__main__':
    main(sys.argv[1])
