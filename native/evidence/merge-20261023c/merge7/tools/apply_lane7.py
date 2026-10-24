#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Apply one lane patch to main (merge wave 7; adapted from merge wave 6's apply_lane.py).
usage: apply_lane7.py <lane> <sha256> <base-source>
base-source: 'head' = the file at d21bd4a5 (git show in main), 'index' = the lane worktree's staged baseline (git show :path).
Per file: if main's file == the lane's before-hash -> git apply --include (check first), after-hash verified.
Otherwise three-way git merge-file (ours = main now, base = the file as the lane saw it (hash-checked against
sha256Before), theirs = lane worktree file == after-hash) under merge-<lane>/; clean results are copied in,
conflicts are left for hand reconciliation (exit 3)."""
import hashlib, json, shutil, subprocess, sys
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); w = root / 'native/work/merge7'
BASE = 'd21bd4a58f489afeafe2a3046ab32a556578f0ec'
lane, want, basesrc = sys.argv[1:4]
wt = Path.home() / 'ClaudeHome/cycles-worktrees' / lane
hd = wt / 'native/work' / lane / 'handoff'
patch = hd / 'changes.patch'
hb = lambda b: hashlib.sha256(b).hexdigest()
h = lambda p: hb(p.read_bytes()) if p.exists() else None
got = h(patch); assert got == want, (got, want)
shutil.copy2(patch, w / 'patches' / f'{lane}.patch')
files = json.load(open(hd / 'changes.json'))['files']
log = []; single, overlap = [], []
for f in files:
    p = f['path']; before = f.get('sha256Before'); after = f.get('sha256After')
    assert h(wt / p) == after, ('lane worktree file != after-hash', p)
    (single if h(root / p) == before else overlap).append(f)
for f in files:
    p = f['path']; dst = w / 'base' / lane / p
    if f.get('sha256Before') is None: continue
    if basesrc == 'head': r = subprocess.run(['git', 'show', f'{BASE}:{p}'], cwd=root, capture_output=True)
    else: r = subprocess.run(['git', 'show', f':{p}'], cwd=wt, capture_output=True)
    assert r.returncode == 0 and hb(r.stdout) == f['sha256Before'], ('base copy != before-hash', p)
    dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(r.stdout)
if single:
    inc = sum((['--include', f['path']] for f in single), [])
    subprocess.run(['git', 'apply', '--check'] + inc + [str(patch)], cwd=root, check=True)
    subprocess.run(['git', 'apply'] + inc + [str(patch)], cwd=root, check=True)
    for f in single:
        ok = h(root / f['path']) == f.get('sha256After'); assert ok, ('after mismatch', f['path'])
        log.append(f"applied {f['path']} -> after-hash match")
conflicts = []
for f in overlap:
    p = f['path']; d = w / f'merge-{lane}' / p; d.parent.mkdir(parents=True, exist_ok=True)
    ours = d.with_name(d.name + '.ours'); base = d.with_name(d.name + '.base'); theirs = d.with_name(d.name + '.theirs')
    shutil.copy2(root / p, ours); shutil.copy2(w / 'base' / lane / p, base); shutil.copy2(wt / p, theirs)
    r = subprocess.run(['git', 'merge-file', '-p', '-L', 'ours(main)', '-L', f'base({lane} before)', '-L', f'theirs({lane})', str(ours), str(base), str(theirs)], capture_output=True)
    d.write_bytes(r.stdout)
    if r.returncode == 0:
        shutil.copy2(d, root / p); log.append(f"three-way {p}: clean")
    else:
        conflicts.append(p); log.append(f"three-way {p}: {r.returncode} conflict(s), working copy merge-{lane}/{p} (NOT copied)")
print(f'lane {lane} patch {got}'); print('\n'.join(log))
(w / f'apply-{lane}.log').write_text(f'lane {lane} patch {got}\n' + '\n'.join(log) + '\n')
if conflicts: print('CONFLICTS:', conflicts); sys.exit(3)
