#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Resolve the conflict blocks of merge-<lane>/<path> (git merge-file output) and copy the result into main.
usage: resolve.py <lane> <path> <spec.py>; spec.py defines blocks = [fn(ours_lines, theirs_lines) -> lines, ...]
(one per conflict block, in order). Helpers available to the spec: OURS, THEIRS, BOTH (ours then theirs), TB (theirs then ours)."""
import sys, shutil
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); w = root / 'native/work/merge8'
lane, path, spec = sys.argv[1:4]
src = w / f'merge-{lane}' / path
L = src.read_text().split('\n')
OURS = lambda o, t: o; THEIRS = lambda o, t: t; BOTH = lambda o, t: o + t; TB = lambda o, t: t + o
g = {'OURS': OURS, 'THEIRS': THEIRS, 'BOTH': BOTH, 'TB': TB}; exec(open(spec).read(), g); blocks = g['blocks']
out = []; i = 0; k = 0
while i < len(L):
    if L[i].startswith('<<<<<<< '):
        j = i + 1; o = []
        while not L[j].startswith('======='): o.append(L[j]); j += 1
        j += 1; t = []
        while not L[j].startswith('>>>>>>> '): t.append(L[j]); j += 1
        out += blocks[k](o, t); k += 1; i = j + 1
    else: out.append(L[i]); i += 1
assert k == len(blocks), (k, len(blocks))
txt = '\n'.join(out); assert '<<<<<<<' not in txt and '>>>>>>>' not in txt
src.with_name(src.name + '.resolved').write_text(txt)
if __import__('os').environ.get('MERGE8_STAGE'): src.with_name(src.name + '.result').write_text(txt)
else: (root / path).write_text(txt)
print(f'{path}: {k} block(s) resolved')
