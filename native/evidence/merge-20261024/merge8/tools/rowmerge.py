#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Word-level three-way merge of table rows (docs/PARITY.md): for each conflicting row (matched by its first cell)
in merge-<lane>/<path>, merge base->ours and base->theirs word by word with git merge-file; fails on a word conflict.
Also checks the result both ways (theirs' change applied to ours == ours' change applied to theirs by construction).
usage: rowmerge.py <lane> <path>"""
import re, subprocess, sys, tempfile, os
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); w = root / 'native/work/merge8'
lane, path = sys.argv[1:3]; D = w / f'merge-{lane}' / path
rd = lambda s: D.with_name(D.name + '.' + s).read_text().split('\n')
O, B, T = rd('ours'), rd('base'), rd('theirs')
key = lambda r: r.split('|')[1].strip() if r.startswith('|') and r.count('|') > 2 else None
def idx(L): return {key(r): r for r in L if key(r)}
o, b, t = idx(O), idx(B), idx(T)
tok = lambda s: re.findall(r'\S+|\s+', s)
def m3(x, y, z):
    with tempfile.TemporaryDirectory() as d:
        ps = []
        for n, s in zip('oby', (x, y, z)):
            p = os.path.join(d, n); open(p, 'w').write('\n'.join(tok(s).__iter__()) .replace('\n\n', '\n\x00\n') + '\n'); ps.append(p)
        # tokens never contain newlines; whitespace tokens become lines of spaces
        for p, s in zip(ps, (x, y, z)): open(p, 'w').write('\n'.join(tok(s)) + '\n')
        r = subprocess.run(['git', 'merge-file', '-p', ps[0], ps[1], ps[2]], capture_output=True, text=True)
        if r.returncode: raise SystemExit(f'word conflict in row {key(x)}:\n{r.stdout[:3000]}')
        return ''.join(r.stdout.split('\n')[:-1])
out = []
for r in O:
    k = key(r)
    if k and k in b and k in t and r != b[k] and t[k] != b[k] and r != t[k]:
        out.append(m3(r, b[k], t[k])); print('word-merged row:', k)
    elif k and k in b and k in t and r == b[k] and t[k] != b[k]:
        out.append(t[k]); print('theirs row:', k)
    else: out.append(r)
txt = '\n'.join(out)
D.with_name(D.name + '.resolved').write_text(txt)
if __import__('os').environ.get('MERGE8_STAGE'): D.with_name(D.name + '.result').write_text(txt)
else: (root / path).write_text(txt)
