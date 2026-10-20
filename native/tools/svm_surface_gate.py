#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Original-CPU surface SVM evaluation comparison and captured GPU replay."""
import argparse
import concurrent.futures
import datetime
import gzip
import importlib.util
import json
import subprocess
from pathlib import Path
from build import ROOT, REPO, WORK, REVISION, PACKAGE
from build_cycles import UPSTREAM
from device_gate import sha, save
import shader_data_gate
import arena_replay

OUT = WORK/'svm-surface/linux-x64'
BASE = WORK/'svm-surface/replay'
DEST = ROOT/'evidence/svm-surface'

def input_hashes():
    hashes = shader_data_gate.input_hashes()
    hashes['docs/RELEASE-LICENSE-PLAN.md'] = sha(REPO/'docs/RELEASE-LICENSE-PLAN.md')
    return hashes

def run(android=False):
    shader_data_gate.run()
    OUT.mkdir(parents=True, exist_ok=True)
    executable = WORK/'cycles-build/bin/codebrix_cycles_svm_surface_gate'
    result = subprocess.run([str(executable), str(OUT/'run')], capture_output=True, text=True, timeout=240)
    (OUT/'run.stdout.txt').write_text(result.stdout)
    (OUT/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(result.stderr)
    report = json.loads(result.stdout)
    assert report['passed'] and report['originalCpuSurfaceSvm']
    expected = {'compiledGraphs':20, 'cases':23, 'paths':8874, 'closureFamilies':5,
                'integerChecks':603432, 'floatChecks':15414138, 'stateChecks':3297280, 'untouchedGuardBytes':2944}
    assert all(report[k] == v for k, v in expected.items()), 'Surface SVM coverage changed'
    assert report['invalidCasesRejected'] == 11 and report['cyclesAllocationBytesAfterCleanup'] == 0
    loaded = Path((OUT/'run.library.txt').read_text()).resolve()
    package = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    assert loaded == package.resolve() and sha(loaded) == sha(package)
    arena_replay.configure(OUT, BASE, '/data/local/tmp/codebrix-cycles-svm-surface')
    devices = [('linux-x64', None)] + (shader_data_gate.ANDROID if android else [])
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(arena_replay.build, rid) for rid in sorted({rid for rid, _ in devices})]:
            future.result()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(arena_replay.run, rid, serial) for rid, serial in devices]:
            future.result()
    folders = [('android-'+serial.replace(':','_')) if serial else 'linux-result' for _, serial in devices]
    save(OUT/'validation.json', {
        'passed':True, 'scope':'Selected surface SVM evaluation/setup from native scene tables and typed bytecode: diffuse, Oren-Nayar, translucent, transparent, emission, holdout and mixtures. Not BSDF sampling, a surface-bounce queue kernel or full session rendering.',
        'results':report, 'strictNumericalParityPassed':report['strictFloatFailures'] == 0,
        'absoluteTolerance':5e-5, 'relativeTolerance':2e-4, 'strictAbsoluteTolerance':1e-7, 'strictRelativeTolerance':1e-7,
        'oracle':'Unmodified CPU shader_setup_from_ray and surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>',
        'matchesPinnedPackage':True, 'loadedWebGpuPath':str(loaded), 'webGpuSha256':sha(loaded),
        'driverSha256':sha(executable), 'cyclesRevision':REVISION, 'sourceSha256':input_hashes(),
        'replayFolders':folders, 'replayScope':'Linux-prepared native buffers, GPU closure evaluation only. Android native Cycles hosts remain pending.',
        'licenseStatus':'Filtered source and independent Apache-2.0 atomics; current dependency audit defines distribution scope.'})
    print(json.dumps(report, indent=2))

def archive():
    validation = json.loads((OUT/'validation.json').read_text())
    if not validation['passed'] or validation['sourceSha256'] != input_hashes():
        raise RuntimeError('Missing/stale surface SVM gate')
    if DEST.exists():
        raise RuntimeError('Checkpoint exists; never overwrite frozen evidence')
    reports = {}
    for name in validation['replayFolders']:
        report = json.loads((BASE/name/'validation.json').read_text())
        if not report['passed'] or any(sha(OUT/k) != v for k, v in report['inputSha256'].items()):
            raise RuntimeError('Missing/stale replay: '+name)
        reports[name] = report
    spec = importlib.util.spec_from_file_location('parity_archive', REPO/'parity/tools/archive.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    copy = module.copy
    DEST.mkdir(parents=True)
    module.snapshot(DEST)  # Includes root Apache text and the adapted BSD notice.
    for path in (WORK/'svm-surface-local-array-failure').glob('*'):
        if path.is_file():
            copy(path, DEST/'initial-local-array-failure'/path.name)
    for path in OUT.iterdir():
        if path.is_file():
            dest = DEST/'linux-x64'/path.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            if path.name.endswith('.arena.bin'):
                dest.with_name(dest.name+'.gz').write_bytes(gzip.compress(path.read_bytes(), mtime=0))
            else:
                copy(path, dest)
    meta = json.loads((OUT/'run.capture.json').read_text())
    offset, length = meta['outputOffset'], meta['recordSize']*meta['count']
    for name, report in reports.items():
        for path in (BASE/name).iterdir():
            if path.is_file() and path.name != 'arena.result.bin':
                copy(path, DEST/'replay'/name/path.name)
        records = (BASE/name/'arena.result.bin').read_bytes()[offset:offset+length]
        (DEST/'replay'/name/'records.bin.gz').write_bytes(gzip.compress(records, mtime=0))
        for path in (BASE/report['rid']).glob('*.json'):
            copy(path, DEST/'replay-build'/report['rid']/path.name)
    # Save source inputs, never the excluded full upstream source tree or binaries.
    for name, digest in validation['sourceSha256'].items():
        path = REPO/name
        if not path.is_file() or sha(path) != digest:
            raise RuntimeError('Source input changed: '+name)
        copy(path, DEST/'source'/name)
    for path in (REPO/'third_party').rglob('*'):
        if path.is_file():
            copy(path, DEST/'source'/path.relative_to(REPO))
    for name in ['cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch']:
        copy(WORK/name, DEST/name)
    for path in (WORK/'cycles-overlay').glob('*.inc'):
        copy(path, DEST/'generated'/path.name)
    provenance = json.loads((REPO/'third_party/provenance.json').read_text())
    for name, digest in provenance['nativeSvmSurfaceDerivations']['sourceSha256'].items():
        path = UPSTREAM/name
        assert sha(path) == digest
        copy(path, DEST/'original-reference'/name)
    for directory in ['device-queue/linux-x64','camera-queue/linux-x64','closest-queue/linux-x64',
                      'background-queue/linux-x64','svm-world/linux-x64','shader-data/linux-x64']:
        source = WORK/directory
        for name in ['validation.json','run.stdout.txt','run.stderr.txt']:
            if (source/name).is_file():
                copy(source/name, DEST/'regressions'/directory/name)
    save(DEST/'manifest.json', {'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'cyclesRevision':REVISION, 'scope':validation['scope'],
        'replayReconstruction':'Decompress linux-x64/run.arena.bin.gz and replace the output span with replay/<device>/records.bin.gz. Reconstruct closureScratch by copying the recordOffset:recordSize closure bytes from each record to its scratch slot. Verify the complete arena against outputSha256 in that replay validation.json.',
        'savedSha256':{str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}})
    print(DEST)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['run','archive'])
    p.add_argument('--android', action='store_true')
    args = p.parse_args()
    if args.action == 'run':
        run(args.android)
    else:
        archive()
