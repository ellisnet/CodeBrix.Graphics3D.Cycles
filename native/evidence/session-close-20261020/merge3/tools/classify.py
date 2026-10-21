#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Classify each lane path: main still at lane's before-hash (single owner) or changed (shared)."""
import hashlib, json
from pathlib import Path
main = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
W = Path('/home/jeremy/ClaudeHome/cycles-worktrees')
pre = json.load(open(main / 'native/work/merge3/pre-merge.json'))['sourceSha256']
def h(p): return hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None
lanes = {'E4': W / 'e4-mode4-adreno/native/work/e4/handoff', 'E5': W / 'e5-pixel-sampler/native/work/e5/handoff', 'M4': W / 'm4-bump-derivative/native/work/m4/handoff'}
out = {}
for lane, hd in lanes.items():
    c = json.load(open(hd / 'changes.json'))
    rows = [(r['path'], r['before'], r['after']) for r in c.get('changed', [])] if 'changed' in c else [(p, v.get('baselineSha256'), v['newSha256']) for p, v in c['changes'].items()]
    for p, b, a in rows:
        src = h(hd / 'source' / p); bef = h(hd / 'before' / p)
        cur = pre.get(p)
        out.setdefault(p, []).append(lane)
        print(f"{lane} {p:55s} main==before:{cur==b} src==after:{src==a} beforecopy==before:{bef==b if bef else 'n/a'}")
print({p: l for p, l in out.items() if len(l) > 1})
