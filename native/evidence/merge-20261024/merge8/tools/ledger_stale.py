#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""List every compatibility/evidence.json checkpoint whose pinned sources (sourceSha256) differ from the tree, with the
changed sources; and whether its report file exists and matches its recorded sha256. usage: ledger_stale.py OUT.txt"""
import hashlib, json, sys
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
cps = json.loads((R/'compatibility/evidence.json').read_text())['checkpoints']
h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
lines = []; stale = 0
for name, c in cps.items():
    changed = [s for s, v in c.get('sourceSha256', {}).items() if h(R/s) != v]
    rep = R/c['report']; rs = 'report ok' if h(rep) == c['sha256'] else ('report MISSING' if not rep.exists() else 'report hash differs')
    if changed: stale += 1
    lines.append(f"{name}: {'STALE' if changed else 'current'}; {rs}; {c['report']}" + (f"\n    changed: {', '.join(changed)}" if changed else ''))
txt = f'{stale} of {len(cps)} checkpoints stale\n' + '\n'.join(lines) + '\n'
Path(sys.argv[1]).write_text(txt); print(txt)
