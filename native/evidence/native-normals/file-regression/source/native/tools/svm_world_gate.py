#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Cycles-compiled world SVM -> original scheduler -> WGSL -> native film."""
import argparse
import datetime
import importlib.util
import json
import subprocess
from pathlib import Path
from build import ROOT, REPO, WORK, REVISION, PACKAGE
from build_cycles import UPSTREAM
from device_gate import sha, save, OUT as CONTROL_OUT
from camera_queue_gate import OUT as CAMERA_OUT
from closest_queue_gate import OUT as CLOSEST_OUT
from background_queue_gate import run as run_background, input_hashes as background_input_hashes, OUT as BACKGROUND_OUT

OUT = WORK/'svm-world/linux-x64'
DEST = ROOT/'evidence/svm-world'
SVM_REFERENCES = ['kernel/svm/'+name+'.h' for name in
    'svm node_types node_types_template types util value geometry tex_coord math math_util clamp convert sepcomb_vector closure checker gradient gamma invert brightness color_util'.split()]
SVM_REFERENCES += ['kernel/geom/shader_data.h', 'kernel/geom/primitive.h', 'kernel/closure/emissive.h', 'util/math_base.h']

def input_hashes():
    hashes = background_input_hashes()
    for path in [REPO/'README.md', REPO/'PARITY.md', ROOT/'DEVICE.md', REPO/'research/THIRD-PARTY-NOTICES.txt',
                 *(p for p in (REPO/'third_party').rglob('*') if p.is_file())]:
        hashes[str(path.relative_to(REPO))] = sha(path)
    return hashes

def run():
    run_background()  # Builds once; checks controls, camera, closest, film and runtime identity.
    OUT.mkdir(parents=True, exist_ok=True)
    executable = WORK/'cycles-build/bin/codebrix_cycles_svm_world_gate'
    result = subprocess.run([str(executable), str(OUT/'run')], capture_output=True, text=True, timeout=180)
    (OUT/'run.stdout.txt').write_text(result.stdout)
    (OUT/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(result.stderr)
    report = json.loads(result.stdout)
    if not all(report[k] for k in ['passed', 'nativeSvmBytecode', 'originalCameraClosestBackgroundScheduler']):
        raise RuntimeError('SVM world gate failed')
    expected = {'compiledWorlds':20, 'cases':181, 'paths':102817, 'opcodesCovered':25,
                'scalarMathOperations':41, 'vectorMathOperations':30, 'conversionKinds':8,
                'invalidCasesRejected':10, 'cyclesAllocationBytesAfterCleanup':0,
                'exactStateChecks':25948160, 'filmFloatChecks':2224128}
    if any(report[k] != v for k, v in expected.items()):
        raise RuntimeError('SVM world coverage changed unexpectedly')
    loaded = Path((OUT/'run.library.txt').read_text()).resolve()
    package = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    if loaded != package.resolve() or sha(loaded) != sha(package):
        raise RuntimeError('SVM world gate loaded a different WebGPU binary')
    save(OUT/'validation.json', {
        'passed':True,
        'scope':'Native Cycles-compiled worlds -> original camera/closest/background scheduler -> typed SVM bytecode in WGSL -> native film. Selected world nodes only; surface/volume shader entries, image textures, derivatives and the remaining SVM nodes are pending.',
        'results':report, 'strictNumericalParityPassed':report['strictFloatFailures'] == 0,
        'absoluteTolerance':5e-5, 'relativeTolerance':2e-4,
        'strictAbsoluteTolerance':1e-7, 'strictRelativeTolerance':1e-7,
        'oracle':'Original CPU integrator_shade_background and SVM interpreter, identical native bytecode/state/film inputs',
        'compiledWorlds':'20 nonconstant graphs compiled by native Cycles, 64x48 pixels, one sample each; raw bytecode and clipped linear PPM previews saved',
        'microPrograms':'161 native typed programs, 257 sparse paths each; signed/zero/unit/near-unit directions, half-angle thresholds, zero divisors, integer bit conversions and both jump outcomes',
        'negativeCases':'10 malformed/unsupported programs: diagnostic bits checked, single-path film and queue counts unchanged; no batch rollback guarantee',
        'matchesPinnedPackage':True, 'loadedWebGpuPath':str(loaded), 'webGpuSha256':sha(loaded),
        'driverSha256':sha(executable), 'cyclesRevision':REVISION, 'sourceSha256':input_hashes(),
        'licenseStatus':'Filtered source and independent Apache-2.0 atomics; current dependency audit defines distribution scope.'})
    print(json.dumps(report, indent=2))

def archive():
    validation = json.loads((OUT/'validation.json').read_text())
    if not validation['passed'] or validation['sourceSha256'] != input_hashes():
        raise RuntimeError('Missing or stale SVM world gate')
    if DEST.exists():
        raise RuntimeError('Checkpoint already exists; never overwrite frozen evidence')
    spec = importlib.util.spec_from_file_location('parity_archive', REPO/'parity/tools/archive.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    copy = module.copy
    DEST.mkdir(parents=True)
    for directory, target in [(OUT, 'linux-x64'), (BACKGROUND_OUT, 'background-regression'),
                              (CLOSEST_OUT, 'closest-regression'), (CAMERA_OUT, 'camera-regression'),
                              (CONTROL_OUT, 'controls-regression')]:
        for path in directory.iterdir():
            if path.is_file():
                copy(path, DEST/target/path.name)
    for name in ['cycles-build.json', 'cycles-webgpu-compatibility.json', 'cycles-registration.patch']:
        copy(WORK/name, DEST/name)
    for name in ['device/device.cpp', 'device/device.h', 'integrator/path_trace.cpp', 'state_copy_reference.inc',
                 'camera_shader.inc', 'closest_shader.inc', 'background_shader.inc', 'svm_enums.inc',
                 'translated.wgsl', 'translation.json', 'translation-cache.json']:
        copy(WORK/'cycles-overlay'/name, DEST/'generated'/name)
    for name in SVM_REFERENCES + ['kernel/integrator/shade_background.h', 'kernel/film/light_passes.h',
                 'kernel/film/data_passes.h', 'kernel/film/write.h', 'kernel/sample/pattern.h',
                 'kernel/integrator/state_template.h', 'integrator/path_trace_work_gpu.cpp']:
        copy(UPSTREAM/'src'/name, DEST/'original-reference'/name)
    commands = json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(DEST/'compile-commands.json', [c for c in commands if str(ROOT) in c['file'] or c['file'].endswith('/path_trace_work_gpu.cpp')])
    module.snapshot(DEST)
    for path in ROOT.rglob('*'):
        if path.is_file() and not any(p in ('work', 'evidence', '__pycache__') for p in path.relative_to(ROOT).parts):
            copy(path, DEST/'source/native'/path.relative_to(ROOT))
    for path in (REPO/'third_party').rglob('*'):
        if path.is_file():
            copy(path, DEST/'source'/path.relative_to(REPO))
    for name in ['README.md', 'PARITY.md', 'THIRD-PARTY-NOTICES.txt', 'AGENTS.md']:
        copy(REPO/name, DEST/'source'/name)
    save(DEST/'manifest.json', {
        'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(), 'cyclesRevision':REVISION,
        'scope':validation['scope'], 'driverSha256':validation['driverSha256'],
        'savedSha256':{str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}})
    print(DEST)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['run', 'archive'])
    args = parser.parse_args()
    if args.action == 'run':
        run()
    else:
        archive()
