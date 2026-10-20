#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Freeze the Phase C close-out as a NEW immutable evidence checkpoint
(native/evidence/phaseC-closeout-20261020). Pattern of Phase C1 freeze_checkpoint.py: validation and
device reports, applied doc diffs, build metadata, final source/binary hashes and a SHA-256 manifest.
No compiled binaries (ELF files are refused). Large payloads (*.bin, files over 1 MB) and per-run WGSL
copies are listed by hash and work location; unique WGSL modules are stored gzip-compressed."""
import datetime, gzip, hashlib, importlib.util, json, shutil, sys
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]; ROOT = REPO/'native'; RUNS = ROOT/'work/phaseC4'
sys.path.insert(0, str(ROOT/'tools'))
from build import REVISION, PACKAGE  # noqa: E402
spec = importlib.util.spec_from_file_location('parity_archive', REPO/'parity/tools/archive.py')
parity = importlib.util.module_from_spec(spec); spec.loader.exec_module(parity)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
SMALL = 1_000_000
dest = ROOT/'evidence/phaseC-closeout-20261020'
if dest.exists(): raise RuntimeError('Checkpoint already exists; never overwrite frozen evidence')
status = json.loads((RUNS/'status.json').read_text())
dest.mkdir(parents=True)
external, modules = {}, {}
paths = []
for name in ['status.json', 'timeline.txt', 'device-timeline.log', 'budgets.txt', 'device-summary.json', 'final-source-sha256.json',
             'staged-android-arm64.json', 'staged-android-arm64-pixel-pre-edit.json', 'staged-android-x64.json', 'staged-hp.json',
             'staged-pixel.json', 'staged-samsung.json', 'start.txt', '01-merge', '02-linux-build', '03-linux-validation', '04-docs',
             '05-audits', 'device-records', 'plans', 'runs', 'tools']:
    top = RUNS/name; paths += [top] if top.is_file() else sorted(top.rglob('*'))
paths += sorted(RUNS.glob('*.log'))
for b in ['build-arm64', 'build-x64/host', 'build-x64/gates']:
    paths.append(RUNS/b/'wrapper.json')
    host = next((RUNS/b).glob('android/*/host'))
    paths += [host/n for n in ['status.json', 'invocation.json', 'webgpu-compatibility.json', 'build.log', 'configure.log']]
refused = []
for path in paths:
    if not path.is_file(): continue
    rel = path.relative_to(RUNS)
    if any(part.startswith('mesa') or part == '__pycache__' for part in rel.parts): continue
    if path.read_bytes()[:4] == b'\x7fELF': refused.append(str(rel)); continue
    if path.suffix == '.wgsl':
        digest = sha(path); external[str(rel)] = {'sha256': digest, 'bytes': path.stat().st_size, 'stored': 'wgsl-modules/'+digest+'.wgsl.gz'}
        if digest not in modules:
            target = dest/'wgsl-modules'/(digest+'.wgsl.gz'); target.parent.mkdir(parents=True, exist_ok=True)
            with path.open('rb') as src, gzip.GzipFile(filename='', mode='wb', fileobj=target.open('wb'), mtime=0) as out: shutil.copyfileobj(src, out)
            modules[digest] = str(rel)
        continue
    if path.suffix == '.bin' or path.stat().st_size > SMALL:
        external[str(rel)] = {'sha256': sha(path), 'bytes': path.stat().st_size, 'workLocation': str(path.relative_to(REPO))}
        continue
    parity.copy(path, dest/'closeout'/rel)
for name in ['cycles-build.json', 'cycles-webgpu-compatibility.json', 'cycles-registration.patch']:
    parity.copy(ROOT/'work'/name, dest/'build'/name)
compile_commands = json.loads((ROOT/'work/cycles-build/compile_commands.json').read_text())
(dest/'build/compile-commands.json').write_text(json.dumps([c for c in compile_commands if str(ROOT) in c['file']], indent=2)+'\n')
parity.snapshot(dest)
for path in ROOT.rglob('*'):
    if not path.is_file() or any(p in ('work', 'evidence', 'binaries', '__pycache__') for p in path.relative_to(ROOT).parts): continue
    parity.copy(path, dest/'source/native'/path.relative_to(ROOT))
for path in [REPO/'README.md', REPO/'PARITY.md']:
    parity.copy(path, dest/'source'/path.relative_to(REPO))
policy = {str(p.relative_to(REPO)): sha(p) for p in [REPO/'THIRD-PARTY-NOTICES.txt', REPO/'NOTICE', REPO/'third_party/dependencies.json',
          REPO/'third_party/provenance.json', REPO/'third_party/license-review-exceptions.json', *sorted((REPO/'tools/licensing').rglob('*.py'))]}
(dest/'external-payloads.json').write_text(json.dumps(external, indent=1)+'\n')
manifest = {'capturedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'cyclesRevision': REVISION,
    'scope': 'Phase C close-out: C3 device-tier merge (hash-verified), Adreno 640 known-adapter entry full with a 20 s budget, closest_queue_gate.py count fix, cold-cache Linux x64 validation of the merged tree, device confirmation on Samsung SM-X700, HP Android x64 and Pixel 4 XL, applied DEVICE.md/PARITY.md/README changes.',
    'webGpuPackage': 'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433',
    'webGpuRuntimeSha256': {rid: sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so') for rid in ['linux-x64', 'android-arm64', 'android-x64']},
    'reconstruction': 'Reports are under closeout/ (paths relative to native/work/phaseC4; Mesa caches and Android build trees excluded). Capture payloads, films and large logs are listed with SHA-256 and work location in external-payloads.json; unique WGSL modules are gzip-compressed under wgsl-modules/. Final Linux binary hashes: closeout/02-linux-build/binaries-final.sha256; Android staged binary hashes: closeout/staged-android-*.json; final source hashes: closeout/final-source-sha256.json. No compiled binaries are included.',
    'refusedElfFiles': refused, 'policyFileSha256': policy,
    'savedSha256': {str(p.relative_to(dest)): sha(p) for p in sorted(dest.rglob('*')) if p.is_file()}}
(dest/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n'); print(dest, len(manifest['savedSha256']), 'files', len(external), 'external', 'refused', refused)
