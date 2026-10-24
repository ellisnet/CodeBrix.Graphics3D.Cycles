#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Apply one lane patch to main (merge wave 8; adapted from merge wave 7's apply_lane7.py).
usage: apply_lane8.py <lane> <sha256>
Base for three-way = the file at aa71c740 (every wave-A lane's base), hash-checked against sha256Before.
Per file: main's file == the lane's before-hash -> git apply --include (check first), after-hash verified.
Otherwise three-way git merge-file (ours = main now, base = aa71c740 file, theirs = lane worktree file == after-hash)
under merge-<lane>/; clean results copied in, conflicts left for hand reconciliation (exit 3).
RENAMES maps a lane path to the path main uses (WebGpuRenderTests.cs -> GpuRenderTests.cs on main)."""
import hashlib, json, shutil, subprocess, sys
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); w = root / 'native/work/merge8'
BASE = 'aa71c74010aca466124b8fedfc5ef5a5dceef75b'
T = 'tests/CodeBrix.Graphics3D.Cycles.Tests/'
RENAMES = {T + 'WebGpuRenderTests.cs': T + 'GpuRenderTests.cs'}
lane, want = sys.argv[1:3]
wt = Path.home() / 'ClaudeHome/cycles-worktrees' / lane
hd = wt / 'native/work' / lane / 'handoff'
patch = hd / 'changes.patch'
hb = lambda b: hashlib.sha256(b).hexdigest()
h = lambda p: hb(p.read_bytes()) if p.exists() else None
got = h(patch); assert got == want, (got, want)
(w / 'patches').mkdir(exist_ok=True); shutil.copy2(patch, w / 'patches' / f'{lane}.patch')
files = json.load(open(hd / 'changes.json'))['files']
log = []; single, overlap = [], []
for f in files:
    p = f['path']; before = f.get('sha256Before'); after = f.get('sha256After')
    assert h(wt / p) == after, ('lane worktree file != after-hash', p)
    if p in RENAMES: overlap.append(f); continue
    if before is None: assert not (root / p).exists(), ('new file exists on main', p)
    (single if h(root / p) == before else overlap).append(f)
for f in files:
    p = f['path']; dst = w / 'base' / lane / p
    if f.get('sha256Before') is None: continue
    r = subprocess.run(['git', 'show', f'{BASE}:{p}'], cwd=root, capture_output=True)
    assert r.returncode == 0 and hb(r.stdout) == f['sha256Before'], ('base copy != before-hash', p)
    dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(r.stdout)
new = []
if single:
    inc = sum((['--include', f['path']] for f in single), [])
    subprocess.run(['git', 'apply', '--check'] + inc + [str(patch)], cwd=root, check=True)
    subprocess.run(['git', 'apply'] + inc + [str(patch)], cwd=root, check=True)
    for f in single:
        ok = h(root / f['path']) == f.get('sha256After'); assert ok, ('after mismatch', f['path'])
        log.append(f"applied {f['path']} -> after-hash match" + (' (NEW)' if f.get('sha256Before') is None else ''))
        if f.get('sha256Before') is None: new.append(f['path'])
if new: subprocess.run(['git', 'add', '-N', '--'] + new, cwd=root, check=True)
conflicts = []
for f in overlap:
    p = f['path']; mp = RENAMES.get(p, p); d = w / f'merge-{lane}' / mp; d.parent.mkdir(parents=True, exist_ok=True)
    ours = d.with_name(d.name + '.ours'); base = d.with_name(d.name + '.base'); theirs = d.with_name(d.name + '.theirs')
    shutil.copy2(root / mp, ours); shutil.copy2(w / 'base' / lane / p, base); shutil.copy2(wt / p, theirs)
    r = subprocess.run(['git', 'merge-file', '-p', '-L', 'ours(main)', '-L', f'base(aa71c740)', '-L', f'theirs({lane})', str(ours), str(base), str(theirs)], capture_output=True)
    d.write_bytes(r.stdout)
    tag = f' (lane path {p})' if mp != p else ''
    if r.returncode == 0:
        shutil.copy2(d, root / mp); log.append(f"three-way {mp}{tag}: clean")
    else:
        conflicts.append(mp); log.append(f"three-way {mp}{tag}: {r.returncode} conflict(s), working copy merge-{lane}/{mp} (NOT copied)")
print(f'lane {lane} patch {got}'); print('\n'.join(log))
(w / f'apply-{lane}.log').write_text(f'lane {lane} patch {got}\n' + '\n'.join(log) + '\n')
if conflicts: print('CONFLICTS:', conflicts); sys.exit(3)
