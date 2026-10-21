#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E1: seal native/work/e1/handoff (changes.patch against the seed manifest,
changes.json with before/after sha256, source/ copies, validation and device
evidence summaries, handoff.json with the patch sha256)."""
import hashlib, json, shutil, subprocess, sys, tempfile, datetime
from pathlib import Path
WT = Path(__file__).resolve().parents[4]; E1 = WT/'native/work/e1'; OUT = E1/'handoff'
SEED = Path('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-phaseE.json')
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
seed = json.loads(SEED.read_text()); before = seed['sourceSha256']
def excluded(rel):
    parts = Path(rel).parts
    return rel.startswith('native/work/') or rel.startswith('native/evidence/') or '.git' in parts or any(p in ('bin', 'obj', '__pycache__') for p in parts)
listed = subprocess.run(['git', '-C', str(WT), 'ls-files', '-co', '--exclude-standard'], capture_output=True, text=True, check=True).stdout.split('\n')
current = {r: sha(WT/r) for r in listed if r and not excluded(r) and (WT/r).is_file()}
seeded = {r: h for r, h in before.items() if not excluded(r)}
modified = sorted(r for r in current if r in seeded and current[r] != seeded[r])
added = sorted(r for r in current if r not in seeded)
deleted = sorted(r for r in seeded if r not in current)
if OUT.exists(): shutil.rmtree(OUT)
(OUT/'source').mkdir(parents=True)
base = E1/'handoff-baseline'
with tempfile.TemporaryDirectory() as tmp:
    a = Path(tmp)/'a'; b = Path(tmp)/'b'
    for r in modified+deleted:
        src = base/r; assert src.exists(), 'missing baseline copy: '+r; assert sha(src) == seeded[r], 'baseline copy differs from seed: '+r
        (a/r).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(src, a/r)
    for r in modified+added:
        (b/r).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(WT/r, b/r)
        (OUT/'source'/r).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(WT/r, OUT/'source'/r)
    a.mkdir(exist_ok=True); b.mkdir(exist_ok=True)
    diff = subprocess.run(['git', 'diff', '--no-index', '--binary', 'a', 'b'], cwd=tmp, capture_output=True, text=True).stdout
    diff = diff.replace(' a/a/', ' a/').replace(' b/b/', ' b/').replace('--- a/a/', '--- a/').replace('+++ b/b/', '+++ b/')
    (OUT/'changes.patch').write_text(diff)
    check = Path(tmp)/'check'; shutil.copytree(a, check)
    subprocess.run(['git', 'init', '-q'], cwd=check, check=True)
    subprocess.run(['git', 'apply', '--check', str(OUT/'changes.patch')], cwd=check, check=True)
    subprocess.run(['git', 'apply', str(OUT/'changes.patch')], cwd=check, check=True)
    for r in modified+added: assert sha(check/r) == current[r], 'patch does not reproduce '+r
changes = {'seedManifest': str(SEED), 'seedHead': seed['head'], 'worktree': str(WT),
           'files': {r: {'before': seeded.get(r), 'after': current.get(r), 'status': 'modified' if r in modified else 'added' if r in added else 'deleted'}
                     for r in modified+added+deleted}}
(OUT/'changes.json').write_text(json.dumps(changes, indent=1)+'\n')
val = OUT/'validation'; val.mkdir()
for pattern in sys.argv[1:]:
    for p in E1.glob(pattern):
        if p.is_file() and p.stat().st_size < 4_000_000 and p.suffix in ('.json', '.jsonl', '.txt', '.log', '.md', '.c', '.cpp', '.py', '.sh'):
            t = val/p.relative_to(E1); t.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, t)
handoff = {'agent': 'phaseE1-triangle-arm', 'worktree': str(WT), 'seedManifest': str(SEED), 'patch': 'changes.patch',
           'patchSha256': sha(OUT/'changes.patch'), 'changesJsonSha256': sha(OUT/'changes.json'),
           'apply': 'git apply --check changes.patch in a tree matching the seed manifest, then git apply changes.patch',
           'counts': {'modified': len(modified), 'added': len(added), 'deleted': len(deleted)},
           'sealed': datetime.datetime.now().isoformat(timespec='seconds')}
(OUT/'handoff.json').write_text(json.dumps(handoff, indent=1)+'\n')
print(json.dumps(handoff, indent=1)); print('modified', modified); print('added', added); print('deleted', deleted)
