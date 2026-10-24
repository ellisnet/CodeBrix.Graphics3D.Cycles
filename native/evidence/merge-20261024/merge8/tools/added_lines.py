#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""For each lane: every line the lane added (aa71c740 -> lane file) must occur in the merged file; lists the lines that
do not (expected only where the merge reconciled the lane's text: renumbered entries, unions, renamed paths).
usage: added_lines.py lane [lane ...]"""
import json, subprocess, sys, difflib
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
T = 'tests/CodeBrix.Graphics3D.Cycles.Tests/'; REN = {T + 'WebGpuRenderTests.cs': T + 'GpuRenderTests.cs'}
for lane in sys.argv[1:]:
    wt = Path.home() / 'ClaudeHome/cycles-worktrees' / lane
    files = json.load(open(wt / 'native/work' / lane / 'handoff/changes.json'))['files']
    print(f'== {lane}')
    for f in files:
        p = f['path']; mp = REN.get(p, p)
        final = set((root / mp).read_text(errors='replace').split('\n'))
        lane_txt = (wt / p).read_text(errors='replace').split('\n')
        base = [] if f.get('sha256Before') is None else subprocess.run(['git', 'show', 'aa71c74010aca466124b8fedfc5ef5a5dceef75b:' + p], cwd=root, capture_output=True, text=True).stdout.split('\n')
        added = [l[1:] for l in difflib.unified_diff(base, lane_txt, lineterm='', n=0) if l.startswith('+') and not l.startswith('+++')]
        miss = [l for l in added if l not in final]
        if miss:
            print(f'  {mp}: {len(miss)} of {len(added)} added lines not verbatim in main')
            for l in miss: print('     -', l[:180])
