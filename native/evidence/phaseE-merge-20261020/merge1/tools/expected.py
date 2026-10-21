#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Normalise the five lanes' changes.json into path -> {lane: (before, after)}; check main against it.
usage: expected.py build | check"""
import hashlib, json, sys
from pathlib import Path
W = Path('/home/jeremy/ClaudeHome/cycles-worktrees')
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
out = root / 'native/work/merge1/expected.json'
lanes = {'e1': 'e1-triangle-arm', 'e2': 'e2-pixel', 'e3': 'e3-host-edits', 'x1': 'x1-android-reference', 'x2': 'x2-exact-traversal'}
def load():
    m = {}
    for lane, wt in lanes.items():
        c = json.load(open(W / wt / 'native/work' / lane / 'handoff/changes.json'))
        if lane == 'e2':
            rows = [(r['path'], r['before'], r['after']) for r in c['changed']] + [(r['path'], None, r['after']) for r in c['added']]
        elif isinstance(c['files'], list):
            rows = [(r['path'], r['beforeSha256'], r['afterSha256']) for r in c['files']]
        else:
            rows = [(p, r['before'], r['after']) for p, r in c['files'].items()]
        for p, b, a in rows:
            m.setdefault(p, {})[lane] = [b, a]
    return m
if sys.argv[1] == 'build':
    m = load(); out.write_text(json.dumps(m, indent=1, sort_keys=True) + '\n')
    shared = {p: sorted(v) for p, v in m.items() if len(v) > 1}
    print(len(m), 'paths;', len(shared), 'shared'); [print(' ', p, v) for p, v in sorted(shared.items())]
else:
    m = json.loads(out.read_text()); bad = 0
    for p, v in sorted(m.items()):
        f = root / p; h = hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else None
        if len(v) == 1:
            (lane, (b, a)), = v.items()
            ok = h == a; tag = 'OWNED ' + lane
        else:
            ok = None; tag = 'SHARED ' + ','.join(sorted(v))
            if any(h == a for b, a in v.values()): ok = False; tag += ' (equals a single lane after-hash: some lane change lost?)'
            if h == next(iter(v.values()))[0]: ok = False; tag += ' (still baseline)'
        if ok is False: bad += 1
        print('OK ' if ok else ('-- ' if ok is None else 'BAD'), tag, p, (h or 'MISSING')[:12])
    print('bad', bad); sys.exit(1 if bad else 0)
