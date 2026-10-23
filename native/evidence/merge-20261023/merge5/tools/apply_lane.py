#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Apply one lane patch to main (merge wave 5). usage: apply_lane.py <lane-dir-name> <short> <sha256>
Single-owner files (not yet changed in this wave): git apply --include (check first), after-hash verified.
Files already changed by an earlier lane in this wave: three-way git merge-file
(ours = main now, base = pre-merge copy saved in base/, theirs = lane worktree file == after-hash)."""
import hashlib, json, shutil, subprocess, sys
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); w = root / 'native/work/merge5'
lane, short, want = sys.argv[1:4]
wt = Path.home() / 'ClaudeHome/cycles-worktrees' / lane
hd = wt / 'native/work' / lane / 'handoff'
patch = hd / 'changes.patch'
h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
got = h(patch); assert got == want, (got, want)
shutil.copy2(patch, w / 'patches' / f'{short}.patch')
files = json.load(open(hd / 'changes.json'))['files']
touched_path = w / 'touched.json'
touched = json.loads(touched_path.read_text()) if touched_path.exists() else {}
log = []
single, overlap = [], []
for f in files:
    p = f['path']; before = f.get('sha256Before'); after = f.get('sha256After')
    assert h(wt / p) == after, ('lane worktree file != after-hash', p)
    if p in touched: overlap.append(f)
    else:
        assert h(root / p) == before, ('main != lane before-hash', p, h(root / p), before)
        single.append(f)
# save pre-merge base copies
for f in single:
    src = root / f['path']
    if src.exists():
        dst = w / 'base' / f['path']; dst.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(src, dst)
if single:
    inc = sum((['--include', f['path']] for f in single), [])
    subprocess.run(['git', 'apply', '--check'] + inc + [str(patch)], cwd=root, check=True)
    subprocess.run(['git', 'apply'] + inc + [str(patch)], cwd=root, check=True)
    for f in single:
        ok = h(root / f['path']) == f.get('sha256After'); assert ok, ('after mismatch', f['path'])
        log.append(f"applied {f['path']} -> after-hash match")
        touched[f['path']] = [short]
conflicts = []
for f in overlap:
    p = f['path']; d = w / f'merge-{short}' / p; d.parent.mkdir(parents=True, exist_ok=True)
    ours = d.with_name(d.name + '.ours'); base = d.with_name(d.name + '.base'); theirs = d.with_name(d.name + '.theirs')
    shutil.copy2(root / p, ours); shutil.copy2(w / 'base' / p, base); shutil.copy2(wt / p, theirs)
    r = subprocess.run(['git', 'merge-file', '-p', '-L', 'ours(main)', '-L', 'base(merge4)', '-L', f'theirs({short})', str(ours), str(base), str(theirs)], capture_output=True)
    d.write_bytes(r.stdout)
    if r.returncode == 0:
        shutil.copy2(d, root / p); log.append(f"three-way {p}: clean"); touched[p].append(short)
    else:
        conflicts.append(p); log.append(f"three-way {p}: {r.returncode} conflict(s), working copy {d.relative_to(w)} (NOT copied)")
touched_path.write_text(json.dumps(touched, indent=1))
print(f'lane {short} patch {got}'); print('\n'.join(log))
if conflicts: print('CONFLICTS:', conflicts); sys.exit(3)
