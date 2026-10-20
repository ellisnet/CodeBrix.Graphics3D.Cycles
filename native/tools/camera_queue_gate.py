#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Original Cycles scheduler -> native camera WGSL -> native path state."""
import argparse
import datetime
import importlib.util
import json
import subprocess
from pathlib import Path
from build import ROOT, REPO, WORK, REVISION, PACKAGE
from build_cycles import UPSTREAM
from device_gate import sha, save, run as run_controls, OUT as CONTROL_OUT
OUT=WORK/'camera-queue/linux-x64'
DEST=ROOT/'evidence/camera-queue'

def input_hashes():
    paths=[p for directory in ['cycles','shaders','src','include','tools'] for p in (ROOT/directory).rglob('*')
           if p.is_file() and '__pycache__' not in p.parts]
    paths+=[REPO/'parity/shaders/sampling.wgsl', REPO/'prototype/native/demo_camera.h']
    return {str(p.relative_to(REPO)):sha(p) for p in sorted(paths)}

def run():
    # Build once and rerun the scheduler controls with the expanded backend.
    # The old frozen checkpoint remains unchanged.
    run_controls()
    OUT.mkdir(parents=True,exist_ok=True)
    executable=WORK/'cycles-build/bin/codebrix_cycles_camera_gate'
    result=subprocess.run([str(executable),str(OUT/'run')],capture_output=True,text=True,timeout=180)
    (OUT/'run.stdout.txt').write_text(result.stdout);(OUT/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    report=json.loads(result.stdout)
    required=['passed','originalSchedulerTileDispatch','directNativeSceneTables','originalCpuCameraOracle','rayDifferentials']
    if not all(report[k] for k in required):raise RuntimeError('Camera integration gate failed')
    expected={'cases':24,'initializedPaths':22972,'retryPaths':65,'invalidCasesRejected':12,'cyclesAllocationBytesAfterCleanup':0}
    if any(report[k]!=v for k,v in expected.items()):raise RuntimeError('Camera coverage changed unexpectedly')
    if report['integerAndGuardChecks']<3000000 or report['floatChecks']<250000 or report['untouchedGuardBytes']<11000000:
        raise RuntimeError('Incomplete state comparison coverage')
    loaded=Path((OUT/'run.library.txt').read_text()).resolve()
    package=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    if loaded!=package.resolve() or sha(loaded)!=sha(package):raise RuntimeError('Camera gate loaded a different WebGPU binary')
    save(OUT/'validation.json',{'passed':True,'scope':'Registered Cycles camera entry through original GPU scheduler; static perspective pinhole, tabulated Sobol, ray differentials, sample counts and adaptive skip. Intersection/shading still pending.',
        'results':report,'strictNumericalParityPassed':report['strictFloatFailures']==0,
        'matchesPinnedPackage':True,'loadedWebGpuPath':str(loaded),'webGpuSha256':sha(loaded),
        'driverSha256':sha(executable),'cyclesRevision':REVISION,'sourceSha256':input_hashes()})
    print(json.dumps(report,indent=2))

def archive():
    validation=json.loads((OUT/'validation.json').read_text())
    if not validation['passed'] or validation['sourceSha256']!=input_hashes():raise RuntimeError('Missing or stale camera gate')
    if DEST.exists():raise RuntimeError('Checkpoint already exists; never overwrite frozen evidence')
    spec=importlib.util.spec_from_file_location('parity_archive',REPO/'parity/tools/archive.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);copy=module.copy
    DEST.mkdir(parents=True)
    for directory,target in [(OUT,'linux-x64'),(CONTROL_OUT,'controls-regression')]:
        for path in directory.iterdir():
            if path.is_file():copy(path,DEST/target/path.name)
    for name in ['cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch']:copy(WORK/name,DEST/name)
    for name in ['device/device.cpp','device/device.h','integrator/path_trace.cpp','state_copy_reference.inc','camera_shader.inc']:
        copy(WORK/'cycles-overlay'/name,DEST/'generated'/name)
    for name in ['kernel/integrator/init_from_camera.h','kernel/integrator/path_state.h','kernel/integrator/state_flow.h',
                 'kernel/integrator/state_util.h','kernel/integrator/state_template.h','kernel/device/gpu/work_stealing.h',
                 'kernel/camera/camera.h','kernel/util/differential.h','kernel/film/adaptive_sampling.h',
                 'kernel/film/light_passes.h','kernel/sample/tabulated_sobol.h','kernel/util/lookup_table.h']:
        copy(UPSTREAM/'src'/name,DEST/'original-reference'/name)
    commands=json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(DEST/'compile-commands.json',[c for c in commands if str(ROOT) in c['file'] or c['file'].endswith('/path_trace_work_gpu.cpp')])
    module.snapshot(DEST)
    for path in ROOT.rglob('*'):
        if path.is_file() and not any(p in ('work','evidence','__pycache__') for p in path.relative_to(ROOT).parts):copy(path,DEST/'source/native'/path.relative_to(ROOT))
    for name in ['README.md','docs/PARITY.md']:copy(REPO/name,DEST/'source'/Path(name).name)
    save(DEST/'manifest.json',{'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cyclesRevision':REVISION,
        'scope':validation['scope'],'driverSha256':validation['driverSha256'],
        'savedSha256':{str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}})
    print(DEST)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['run','archive']);args=parser.parse_args()
    if args.action=='run':run()
    else:archive()
