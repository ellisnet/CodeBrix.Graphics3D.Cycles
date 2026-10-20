#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native surface shader-data comparison and optional Android GPU replay."""
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
from device_gate import sha, save, OUT as CONTROL_OUT
from camera_queue_gate import OUT as CAMERA_OUT
from closest_queue_gate import OUT as CLOSEST_OUT
from background_queue_gate import OUT as BACKGROUND_OUT
from svm_world_gate import run as run_svm, input_hashes as svm_input_hashes, OUT as SVM_OUT
import arena_replay

OUT = WORK/'shader-data/linux-x64'
DEST = ROOT/'evidence/shader-data'
REFERENCES = ['kernel/geom/'+n+'.h' for n in ['shader_data','triangle','triangle_intersect','object','attribute']]
REFERENCES += ['kernel/util/differential.h', 'util/transform.h', 'util/types_normal.h', 'util/math_float3.h']
ANDROID = [('android-arm64','98271FFBA0048D'), ('android-arm64','R52W207XQ1V'), ('android-x64','192.168.86.225:5555')]

def input_hashes():
    hashes = svm_input_hashes()
    for path in (ROOT/'tests').rglob('*'):
        if path.is_file() and '__pycache__' not in path.parts:
            hashes[str(path.relative_to(REPO))] = sha(path)
    return hashes

def run(android=False):
    run_svm()
    OUT.mkdir(parents=True, exist_ok=True)
    executable = WORK/'cycles-build/bin/codebrix_cycles_shader_data_gate'
    result = subprocess.run([str(executable), str(OUT/'run')], capture_output=True, text=True, timeout=180)
    (OUT/'run.stdout.txt').write_text(result.stdout)
    (OUT/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(result.stderr)
    report = json.loads(result.stdout)
    expected = {'cases':11, 'paths':12066, 'cameraHits':240, 'hitObjects':9, 'integerChecks':84462,
                'floatChecks':337848, 'stateChecks':1576960, 'untouchedGuardBytes':5324,
                'invalidCasesRejected':12, 'cyclesAllocationBytesAfterCleanup':0}
    assert all(report[k] for k in ['passed','originalCpuShaderSetup','nativeSceneTables'])
    assert all(report[k] == v for k, v in expected.items()), 'Shader-data coverage changed'
    assert all(report['projectionAxes']) and all(report[k] for k in
        ['backfacing','smooth','cornerNormals','instanceTransforms','appliedTransforms','negativeScale','negativePositionOffsets','negativeNormalOffsets'])
    loaded = Path((OUT/'run.library.txt').read_text()).resolve()
    package = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    assert loaded == package.resolve() and sha(loaded) == sha(package)
    devices = [('linux-x64', None)] + (ANDROID if android else [])
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(arena_replay.build, rid) for rid in sorted({rid for rid, _ in devices})]:
            future.result()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(arena_replay.run, rid, serial) for rid, serial in devices]:
            future.result()
    folders = [('android-'+serial.replace(':','_')) if serial else 'linux-result' for _, serial in devices]
    save(OUT/'validation.json', {
        'passed':True,
        'scope':'Native static-triangle shader_setup_from_ray: original camera/closest hits plus controlled directions and signed attribute offsets. Smooth/corner packed normals, instance/applied/mirrored transforms, backfaces, compact ray and UV differentials. Not a surface-bounce or closure evaluation gate.',
        'results':report, 'strictNumericalParityPassed':report['strictFloatFailures'] == 0,
        'absoluteTolerance':5e-5, 'relativeTolerance':2e-4, 'strictAbsoluteTolerance':1e-7, 'strictRelativeTolerance':1e-7,
        'oracle':'Unmodified CPU shader_setup_from_ray reading identical Cycles scene and path tables',
        'matchesPinnedPackage':True, 'loadedWebGpuPath':str(loaded), 'webGpuSha256':sha(loaded),
        'driverSha256':sha(executable), 'cyclesRevision':REVISION, 'sourceSha256':input_hashes(),
        'replayFolders':folders, 'replayScope':'One 1,314-path buffer capture, GPU shader execution only; Android host preparation and full Cycles Android builds remain pending.',
        'licenseStatus':'Filtered source and independent Apache-2.0 atomics; current dependency audit defines distribution scope.'})
    print(json.dumps(report, indent=2))

def archive():
    validation = json.loads((OUT/'validation.json').read_text())
    if not validation['passed'] or validation['sourceSha256'] != input_hashes():
        raise RuntimeError('Missing/stale shader-data gate')
    if DEST.exists():
        raise RuntimeError('Checkpoint exists; never overwrite frozen evidence')
    reports = {}
    for name in validation['replayFolders']:
        report = json.loads((arena_replay.BASE/name/'validation.json').read_text())
        if not report['passed'] or any(sha(OUT/k) != v for k, v in report['inputSha256'].items()):
            raise RuntimeError('Missing/stale replay: '+name)
        reports[name] = report
    spec = importlib.util.spec_from_file_location('parity_archive', REPO/'parity/tools/archive.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    copy = module.copy
    DEST.mkdir(parents=True)
    for directory, target in [(OUT,'linux-x64'), (SVM_OUT,'svm-regression'), (BACKGROUND_OUT,'background-regression'),
                              (CLOSEST_OUT,'closest-regression'), (CAMERA_OUT,'camera-regression'), (CONTROL_OUT,'controls-regression')]:
        for path in directory.iterdir():
            if path.is_file():
                if directory == OUT and path.name.endswith('.arena.bin'):
                    dest = DEST/target/(path.name+'.gz')
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(gzip.compress(path.read_bytes(), mtime=0))
                else:
                    copy(path, DEST/target/path.name)
    capture = json.loads((OUT/'run.capture.json').read_text())
    offset, length = capture['outputOffset'], capture['recordSize']*capture['count']
    for name, report in reports.items():
        for path in (arena_replay.BASE/name).iterdir():
            if path.is_file() and path.name != 'arena.result.bin':
                copy(path, DEST/'replay'/name/path.name)
        records = (arena_replay.BASE/name/'arena.result.bin').read_bytes()[offset:offset+length]
        (DEST/'replay'/name/'records.bin.gz').write_bytes(gzip.compress(records, mtime=0))
        for path in (arena_replay.BASE/report['rid']).glob('*.json'):
            copy(path, DEST/'replay-build'/report['rid']/path.name)
    for name in ['cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch']:
        copy(WORK/name, DEST/name)
    for name in ['device/device.cpp','device/device.h','integrator/path_trace.cpp','state_copy_reference.inc',
                 'camera_shader.inc','closest_shader.inc','background_shader.inc','shader_data_shader.inc',
                 'svm_enums.inc','translated.wgsl','translation.json','translation-cache.json']:
        copy(WORK/'cycles-overlay'/name, DEST/'generated'/name)
    for name in REFERENCES + ['kernel/types.h','kernel/integrator/state_template.h','kernel/integrator/state_util.h']:
        copy(UPSTREAM/'src'/name, DEST/'original-reference'/name)
    commands = json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(DEST/'compile-commands.json', [c for c in commands if str(ROOT) in c['file'] or c['file'].endswith('/path_trace_work_gpu.cpp')])
    module.snapshot(DEST)
    for path in ROOT.rglob('*'):
        if path.is_file() and not any(p in ('work','evidence','__pycache__') for p in path.relative_to(ROOT).parts):
            copy(path, DEST/'source/native'/path.relative_to(ROOT))
    for path in (REPO/'third_party').rglob('*'):
        if path.is_file():
            copy(path, DEST/'source'/path.relative_to(REPO))
    for name in ['README.md','docs/PARITY.md','THIRD-PARTY-NOTICES.txt','AGENTS.md']:
        copy(REPO/name, DEST/'source'/name)
    save(DEST/'manifest.json', {
        'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(), 'cyclesRevision':REVISION,
        'scope':validation['scope'], 'driverSha256':validation['driverSha256'],
        'replayReconstruction':'Decompress linux-x64/run.arena.bin.gz; replace outputOffset:outputOffset+count*recordSize with replay/<device>/records.bin.gz. The reconstructed arena must match outputSha256 in that replay validation.json. No native dependency binaries archived.',
        'savedSha256':{str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}})
    print(DEST)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['run','archive'])
    p.add_argument('--android', action='store_true', help='Replay on the three authorized connected devices')
    args = p.parse_args()
    if args.action == 'run':
        run(args.android)
    else:
        archive()
