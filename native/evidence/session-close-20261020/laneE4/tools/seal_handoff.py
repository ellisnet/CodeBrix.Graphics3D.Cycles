#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4 (copy of Phase E2's sealer): seal native/work/e4/handoff (patch vs baseline-merge1.json, before/after hashes,
sources, validation, device evidence). Changed paths are found by hashing every seed-manifest path."""
import hashlib, json, shutil, subprocess, tempfile, datetime
from pathlib import Path
E = Path(__file__).resolve().parents[1]; REPO = E.parents[2]; H = E/'handoff'
MANIFEST = '/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-merge1.json'
SEED = json.loads(Path(MANIFEST).read_text())['sourceSha256']
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
changed = sorted(p for p, h in SEED.items() if sha(REPO/p) != h)
changes = []; patch = ''
for rel in changed:
    before = H/'before'/rel; assert sha(before) == SEED[rel], rel
    changes.append(dict(path=rel, before=SEED[rel], after=sha(REPO/rel)))
    patch += subprocess.run(['diff', '-u', '--label', 'a/'+rel, '--label', 'b/'+rel, str(before), str(REPO/rel)], capture_output=True, text=True).stdout
(H/'changes.patch').write_text(patch)
(H/'changes.json').write_text(json.dumps(dict(seedManifest=MANIFEST, changed=changes, added=[], removed=[]), indent=1)+'\n')
with tempfile.TemporaryDirectory(dir=str(E)) as t:
    for rel in changed: (Path(t)/rel).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(H/'before'/rel, Path(t)/rel)
    subprocess.run(['patch', '-p1', '-s', '-i', str(H/'changes.patch')], cwd=t, check=True)
    ok = all(sha(Path(t)/c['path']) == c['after'] for c in changes)
src = H/'source'; shutil.rmtree(src, ignore_errors=True)
for c in changes: (src/c['path']).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(REPO/c['path'], src/c['path'])
files = sorted(p for p in H.rglob('*') if p.is_file() and p.name != 'handoff.json')
(H/'handoff.json').write_text(json.dumps(dict(agent='Phase E4 (mode 4 on Adreno, Opus 5.5)', sealed=datetime.datetime.now().isoformat(timespec='seconds'),
    worktree=str(REPO), head='01eb27b6', seedManifest=MANIFEST, seedManifestSha256=sha(MANIFEST), patchSha256=sha(H/'changes.patch'),
    patchAppliesToSeedAndReproducesAfterHashes=ok, changed=len(changes), added=0, removed=0,
    files={str(p.relative_to(H)): sha(p) for p in files}), indent=1)+'\n')
print('sealed', len(files), 'files; patch check', ok, 'changed', len(changes))
