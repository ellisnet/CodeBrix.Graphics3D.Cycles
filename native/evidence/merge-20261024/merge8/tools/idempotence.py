#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Check that every lane's changes are present in the merged tree: for each file of each lane patch, three-way merge
(ours = main now, base = aa71c740 file, theirs = lane file). A file whose merge returns main unchanged holds the lane's
hunks; otherwise the differing lines are listed (expected only where the merge reconciled the lane's text).
usage: idempotence.py lane [lane ...]"""
import json, subprocess, sys, tempfile, os
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
T = 'tests/CodeBrix.Graphics3D.Cycles.Tests/'; REN = {T + 'WebGpuRenderTests.cs': T + 'GpuRenderTests.cs'}
for lane in sys.argv[1:]:
    wt = Path.home() / 'ClaudeHome/cycles-worktrees' / lane
    files = json.load(open(wt / 'native/work' / lane / 'handoff/changes.json'))['files']
    same = 0; diff = []
    for f in files:
        p = f['path']; mp = REN.get(p, p)
        ours = (root / mp).read_bytes(); theirs = (wt / p).read_bytes()
        if f.get('sha256Before') is None:
            (same := same + 1) if ours == theirs else diff.append((mp, 'new file differs from lane')); continue
        base = subprocess.run(['git', 'show', 'aa71c74010aca466124b8fedfc5ef5a5dceef75b:' + p], cwd=root, capture_output=True).stdout
        with tempfile.TemporaryDirectory() as d:
            po, pb, pt = (os.path.join(d, n) for n in 'obt')
            open(po, 'wb').write(ours); open(pb, 'wb').write(base); open(pt, 'wb').write(theirs)
            r = subprocess.run(['git', 'merge-file', '-p', po, pb, pt], capture_output=True)
        if r.returncode == 0 and r.stdout == ours: same += 1
        else:
            o = ours.decode(errors='replace').split('\n'); m = r.stdout.decode(errors='replace').split('\n')
            import difflib
            dl = [l for l in difflib.unified_diff(o, m, lineterm='', n=0) if l[:1] in '+-' and not l.startswith(('+++', '---'))]
            diff.append((mp, f'{r.returncode} conflict(s), {len(dl)} differing lines'))
    print(f'{lane}: {same}/{len(files)} files hold the lane change unchanged; differing: {len(diff)}')
    for d in diff: print('   ', *d)
