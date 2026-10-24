#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Install the staged m2-mnee merge (merge-m2-mnee/<path>.result) into main: checks that main's file still equals the
'ours' side the staging used (or is absent for new files), that single-owner/new results equal the lane after-hash,
writes the results and registers new files with git add -N."""
import hashlib, json, shutil, subprocess
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); S = root/'native/work/merge8/merge-m2-mnee'
wt = Path.home()/'ClaudeHome/cycles-worktrees/m2-mnee'
files = json.load(open(wt/'native/work/m2-mnee/handoff/changes.json'))['files']
h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
new = []; log = []
for f in files:
    p = f['path']; res = S/(p+'.result'); ours = S/(p+'.ours')
    if h(root/p) == h(res):
        if f.get('sha256Before') is None: new.append(p)
        log.append(p); continue  # already installed (re-run)
    if f.get('sha256Before') is None: assert not (root/p).exists(); new.append(p); assert h(res) == f['sha256After']
    elif ours.exists(): assert h(root/p) == h(ours), ('main moved since staging', p)
    else: assert h(root/p) == f['sha256Before'] and h(res) == f['sha256After'], p
    (root/p).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(res, root/p); log.append(p)
subprocess.run(['git', 'add', '-N', '--'] + new, cwd=root, check=True)
print(len(log), 'files installed;', len(new), 'new (git add -N)')
