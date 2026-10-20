#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Freeze the Phase C validated integration as an immutable evidence checkpoint.

Pattern of native/tools/archive.py / device_gate.py archive: active native source
snapshot, validation reports, build metadata, runtime/binary hashes and a saved
SHA-256 manifest. No compiled binaries. Large capture payloads (*.bin arenas,
command streams, films) and per-run WGSL module copies are recorded by hash and
work-tree location; unique WGSL modules are stored once, gzip-compressed."""
import argparse, datetime, gzip, hashlib, importlib.util, json, shutil, sys
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]; ROOT = REPO/'native'; WORK = ROOT/'work'
sys.path.insert(0, str(ROOT/'tools'))
from build import REVISION, PACKAGE  # noqa: E402
spec = importlib.util.spec_from_file_location('parity_archive', REPO/'parity/tools/archive.py')
parity = importlib.util.module_from_spec(spec); spec.loader.exec_module(parity)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
SMALL = 1_000_000

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--runs', type=Path, required=True, help='phaseC validation root')
    ap.add_argument('--build-record', type=Path, required=True); a = ap.parse_args()
    dest = ROOT/'evidence/native-integration-20261019'
    if dest.exists(): raise RuntimeError('Checkpoint already exists; never overwrite frozen evidence')
    status = json.loads((a.runs/'status.json').read_text())
    if not status.get('validationPassed'): raise RuntimeError('Phase C validation has not passed')
    dest.mkdir(parents=True)
    external, modules = {}, {}
    selected = ['status.json', 'cold-compile-findings.txt', 'source-audit.json', '01-b3', '02-b1', '03-b2',
                '04-clean-rebuild', '05-validation', '05-validation-r2', '06-audits', 'tools']
    paths = []
    for name in selected:
        top = a.runs/name
        paths += [top] if top.is_file() else sorted(top.rglob('*'))
    for path in paths:
        if not path.is_file() or any(part.startswith('mesa') or part == '__pycache__' for part in path.relative_to(a.runs).parts): continue
        rel = path.relative_to(a.runs)
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
        parity.copy(path, dest/'validation'/rel)
    parity.copy(a.build_record, dest/'build'/a.build_record.name)
    for name in ['cycles-build.json', 'cycles-webgpu-compatibility.json', 'cycles-registration.patch']:
        parity.copy(WORK/name, dest/'build'/name)
    compile_commands = json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    (dest/'build/compile-commands.json').write_text(json.dumps([c for c in compile_commands if str(ROOT) in c['file']], indent=2)+'\n')
    for name in ['svm_shader.inc', 'surface_shader.inc', 'shadow_shader.inc', 'background_shader.inc', 'camera_shader.inc', 'closest_shader.inc',
                 'shader_data_shader.inc', 'volume_shader.inc', 'volume_stack_shader.inc', 'svm_enums.inc', 'translation.json']:
        if (WORK/'cycles-overlay'/name).exists(): parity.copy(WORK/'cycles-overlay'/name, dest/'generated'/name)
    parity.snapshot(dest)
    for path in ROOT.rglob('*'):
        if not path.is_file() or any(p in ('work', 'evidence', 'binaries', '__pycache__') for p in path.relative_to(ROOT).parts): continue
        parity.copy(path, dest/'source/native'/path.relative_to(ROOT))
    for path in [REPO/'README.md', REPO/'PARITY.md']:
        parity.copy(path, dest/'source'/path.relative_to(REPO))
    # License-policy and generated attribution files carry reviewed restricted-license
    # wording that is approved only at their repository paths; record hashes instead.
    policy = {str(p.relative_to(REPO)): sha(p) for p in [REPO/'THIRD-PARTY-NOTICES.txt', REPO/'NOTICE', REPO/'third_party/dependencies.json',
              REPO/'third_party/provenance.json', REPO/'third_party/license-review-exceptions.json', *sorted((REPO/'tools/licensing').rglob('*.py'))]}
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    (dest/'external-payloads.json').write_text(json.dumps(external, indent=1)+'\n')
    manifest = {'capturedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'cyclesRevision': REVISION,
        'scope': 'Phase C Linux x64 integration of B3 scene specialization/lighting split/pipeline tiers, B1 emissive triangle lights and transparent-shadow ShaderData split, B2 ARM normalization mode and cosine-hemisphere/valid-normal adapters; cold Mesa shader caches.',
        'webGpuPackage': 'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433', 'webGpuRuntimeSha256': sha(runtime),
        'reconstruction': 'Validation reports are under validation/ (paths relative to native/work/phaseC; Mesa shader caches excluded). Capture payloads and per-run WGSL copies are listed with SHA-256 in external-payloads.json; unique WGSL modules are stored gzip-compressed under wgsl-modules/. Binary hashes are in build/. No compiled binaries are included.',
        'policyFileSha256': policy,
        'savedSha256': {str(p.relative_to(dest)): sha(p) for p in sorted(dest.rglob('*')) if p.is_file()}}
    (dest/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n'); print(dest)

if __name__ == '__main__': main()
