#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E5: build and seal native/work/e5/handoff (patch vs baseline-merge1.json, before/after hashes,
sources, validation and device evidence, budgets, timeline)."""
import hashlib, json, shutil, subprocess, tempfile, datetime
from pathlib import Path
E = Path(__file__).resolve().parents[1]; REPO = E.parents[2]; H = E/'handoff'
MANIFEST = '/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-merge1.json'
SEED = json.loads(Path(MANIFEST).read_text())['sourceSha256']
B = E/'before-source'
CHANGED = {'native/cycles/emitter_sample_gate.cpp': B/'emitter_sample_gate.cpp',
           'native/DEVICE.md': B/'docs/DEVICE.md', 'docs/PARITY.md': B/'docs/PARITY.md'}
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
# Every other seeded path must be unchanged.
others = [rel for rel, h in SEED.items() if rel not in CHANGED and sha(REPO/rel) != h]
assert not others, others
if H.exists(): shutil.rmtree(H)
H.mkdir()
changes = []; patch = ''
for rel, before in CHANGED.items():
    assert sha(before) == SEED[rel], rel
    changes.append(dict(path=rel, before=sha(before), after=sha(REPO/rel)))
    patch += subprocess.run(['diff', '-u', '--label', 'a/'+rel, '--label', 'b/'+rel, str(before), str(REPO/rel)], capture_output=True, text=True).stdout
(H/'changes.patch').write_text(patch)
(H/'changes.json').write_text(json.dumps(dict(seedManifest=MANIFEST, seedManifestSha256=sha(MANIFEST), changed=changes, added=[], removed=[]), indent=1)+'\n')
with tempfile.TemporaryDirectory(dir=str(E)) as t:
    for rel, before in CHANGED.items():
        (Path(t)/rel).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(before, Path(t)/rel)
    applied = subprocess.run(['git', 'apply', '--check', str(H/'changes.patch')], cwd=t).returncode == 0
    subprocess.run(['git', 'apply', str(H/'changes.patch')], cwd=t, check=True)
    ok = applied and all(sha(Path(t)/c['path']) == c['after'] for c in changes)
for c in changes:
    (H/'source'/c['path']).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(REPO/c['path'], H/'source'/c['path'])
    (H/'before'/c['path']).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(CHANGED[c['path']], H/'before'/c['path'])
def copy(src, dst, patterns):
    for pat in patterns:
        for p in Path(src).glob(pat):
            if p.is_file() and p.stat().st_size < 4_000_000:
                q = H/dst/p.relative_to(src); q.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, q)
V = E/'linux'
copy(V, 'validation/linux', ['*/exit.json', '*/slot.stdout.txt', '*/slot.stderr.txt', '*.json', '*.txt', 'triangle/out/results.json', 'triangle/out/status.json',
                             'triangle/out/triangle_sample/stdout.txt', 'all-lanes/out/status.json', 'all-lanes/out/gate-exit-codes.json', 'all-lanes/out/*.stdout.txt',
                             'emitter-sample/run.*.bin', 'triangle/out/triangle_sample/run.actual.bin', 'host-gate/out/*.json'])
copy(E/'audits', 'validation/audits', ['*'])
copy(E, 'validation/builds', ['android-build-*.log', 'staged-binaries-*.json', 'staged-pixel.json'])
R = E/'runs/pixel'
copy(R, 'device/runs', ['*/results.json', '*/status.json', '*/*.scene-reference.txt', '*/device-scene-reference/*.provenance.json',
                        '*/output/triangle_sample/run.actual.bin', '*/output/emitter_sample/run.actual.bin', '*/*/output/*/run/*.json'])
copy(E/'device', 'device/chains', ['*.log'])
copy(E/'probe', 'device/probe', ['sampler_probe.cpp', 'build_probe.py', 'mkvar.py', 'judge.py', 'base.wgsl', 'var/*.json',
                                 'runs/*/stderr.txt', 'runs/*/stdout.txt', 'runs/*/rc.txt', 'runs/*/pulled/run.e5-error.txt'])
copy(E/'tools', 'tools', ['*'])
for name in ['budgets.txt', 'timeline.log', 'scratch-deleted.json']: shutil.copy2(E/name, H/name)
files = sorted(p for p in H.rglob('*') if p.is_file() and p.name != 'handoff.json')
(H/'handoff.json').write_text(json.dumps(dict(agent='Phase E5 (Pixel 4 XL triangle sampler, Opus 5.5)', sealed=datetime.datetime.now().isoformat(timespec='seconds'),
    worktree=str(REPO), head='01eb27b6', seedManifest=MANIFEST, seedManifestSha256=sha(MANIFEST), patchSha256=sha(H/'changes.patch'),
    patchAppliesToSeedAndReproducesAfterHashes=ok, changed=len(changes), otherSeedPathsUnchanged=True,
    files={str(p.relative_to(H)): sha(p) for p in files}), indent=1)+'\n')
print('sealed', len(files), 'files; patch check', ok, 'patch sha256', sha(H/'changes.patch'))
