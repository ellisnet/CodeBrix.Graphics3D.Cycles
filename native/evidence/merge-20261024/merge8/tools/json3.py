#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Three-way JSON merge for compatibility/{reviews,evidence}.json: recursive over dicts; a key changed on one side only
takes that side; changed on both sides identically is fine; a real conflict aborts. Lists are leaves.
usage: json3.py <lane> <path> [old=new string replacement applied to theirs ...]"""
import json, sys
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); w = root / 'native/work/merge8'
lane, path = sys.argv[1:3]; reps = [a.split('=', 1) for a in sys.argv[3:]]
D = w / f'merge-{lane}' / path
load = lambda s: json.loads(D.with_name(D.name + '.' + s).read_text())
tt = D.with_name(D.name + '.theirs').read_text()
for a, b in reps: tt = tt.replace(a, b)
o, b, t = load('ours'), load('base'), json.loads(tt)
M = object()
def m(o, b, t, k=''):
    if o == t: return o
    if b == o: return t
    if b == t: return o
    if all(isinstance(x, dict) for x in (o, t)) and (b is M or isinstance(b, dict)):
        bb = {} if b is M else b; r = {}
        for key in list(o) + [x for x in t if x not in o]:
            v = m(o.get(key, M), bb.get(key, M), t.get(key, M), k + '/' + key)
            if v is not M: r[key] = v
        return r
    raise SystemExit(f'conflict at {k}')
r = m(o, b, t)
txt = json.dumps(r, indent=2, ensure_ascii=False) + '\n'
D.with_name(D.name + '.resolved').write_text(txt)
if __import__('os').environ.get('MERGE8_STAGE'): D.with_name(D.name + '.result').write_text(txt)
else: (root / path).write_text(txt); print(path, 'merged')
