#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Original camera -> closest -> constant world background -> native film."""
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
from closest_queue_gate import run as run_closest, input_hashes as closest_input_hashes, OUT as CLOSEST_OUT
OUT=WORK/'background-queue/linux-x64'
DEST=ROOT/'evidence/background-queue'

def input_hashes():
    hashes=closest_input_hashes()
    for path in [REPO/'THIRD-PARTY-NOTICES.txt',REPO/'AGENTS.md',ROOT/'THIRD-PARTY-NOTICES.txt']:
        hashes[str(path.relative_to(REPO))]=sha(path)
    return hashes

def run():
    run_closest()  # Builds once, checks controls, camera, and package identity.
    OUT.mkdir(parents=True,exist_ok=True)
    executable=WORK/'cycles-build/bin/codebrix_cycles_background_gate'
    result=subprocess.run([str(executable),str(OUT/'run')],capture_output=True,text=True,timeout=180)
    (OUT/'run.stdout.txt').write_text(result.stdout);(OUT/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    report=json.loads(result.stdout)
    if not report['passed'] or not report['originalCameraClosestBackgroundScheduler']:raise RuntimeError('Background gate failed')
    expected={'cases':26,'paths':55681,'invalidCasesRejected':12,'cyclesAllocationBytesAfterCleanup':0}
    if any(report[k]!=v for k,v in expected.items()):raise RuntimeError('Background coverage changed unexpectedly')
    if report['exactStateChecks']<3900000 or report['filmFloatChecks']<3800000:raise RuntimeError('Incomplete background comparisons')
    loaded=Path((OUT/'run.library.txt').read_text()).resolve()
    package=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    if loaded!=package.resolve() or sha(loaded)!=sha(package):raise RuntimeError('Background gate loaded a different WebGPU binary')
    save(OUT/'validation.json',{'passed':True,
        'scope':'Native scene -> original scheduler camera -> closest -> constant background -> film. Combined/transparency, background, lightgroups, diffuse/glossy/transmission direct/indirect, adaptive auxiliary and static background data passes. SVM worlds, sun/MIS and surface shading remain pending.',
        'results':report,'strictNumericalParityPassed':report['strictFloatFailures']==0,
        'absoluteTolerance':5e-5,'relativeTolerance':2e-4,'strictAbsoluteTolerance':1e-7,'strictRelativeTolerance':1e-7,
        'oracle':'Original integrator_shade_background, identical input state and native shader/film tables',
        'matchesPinnedPackage':True,'loadedWebGpuPath':str(loaded),'webGpuSha256':sha(loaded),
        'driverSha256':sha(executable),'cyclesRevision':REVISION,'sourceSha256':input_hashes(),
        'licenseStatus':'Filtered Cycles source and independent Apache-2.0 atomics; current dependency and binary audits define distribution readiness. No native binaries archived.'})
    print(json.dumps(report,indent=2))

def archive():
    validation=json.loads((OUT/'validation.json').read_text())
    if not validation['passed'] or validation['sourceSha256']!=input_hashes():raise RuntimeError('Missing or stale background gate')
    if DEST.exists():raise RuntimeError('Checkpoint already exists; never overwrite frozen evidence')
    spec=importlib.util.spec_from_file_location('parity_archive',REPO/'parity/tools/archive.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);copy=module.copy
    DEST.mkdir(parents=True)
    for directory,target in [(OUT,'linux-x64'),(CLOSEST_OUT,'closest-regression'),(CAMERA_OUT,'camera-regression'),(CONTROL_OUT,'controls-regression')]:
        for path in directory.iterdir():
            if path.is_file():copy(path,DEST/target/path.name)
    for name in ['cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch']:copy(WORK/name,DEST/name)
    for name in ['device/device.cpp','device/device.h','integrator/path_trace.cpp','state_copy_reference.inc',
                 'camera_shader.inc','closest_shader.inc','background_shader.inc','translated.wgsl','translation.json','translation-cache.json']:
        copy(WORK/'cycles-overlay'/name,DEST/'generated'/name)
    for name in ['kernel/integrator/shade_background.h','kernel/film/light_passes.h','kernel/film/data_passes.h',
                 'kernel/film/write.h','kernel/sample/pattern.h','kernel/light/common.h',
                 'kernel/integrator/intersect_closest.h','kernel/integrator/path_state.h','kernel/integrator/state_flow.h',
                 'kernel/integrator/state_util.h','kernel/integrator/state_template.h','kernel/bvh/traversal.h',
                 'kernel/bvh/nodes.h','kernel/bvh/bvh.h','kernel/bvh/util.h','kernel/bvh/types.h',
                 'kernel/geom/object.h','kernel/geom/triangle_intersect.h','util/math_intersect.h',
                 'kernel/sample/tabulated_sobol.h','integrator/path_trace_work_gpu.cpp']:
        copy(UPSTREAM/'src'/name,DEST/'original-reference'/name)
    commands=json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(DEST/'compile-commands.json',[c for c in commands if str(ROOT) in c['file'] or c['file'].endswith('/path_trace_work_gpu.cpp')])
    module.snapshot(DEST)
    for path in ROOT.rglob('*'):
        if path.is_file() and not any(p in ('work','evidence','__pycache__') for p in path.relative_to(ROOT).parts):copy(path,DEST/'source/native'/path.relative_to(ROOT))
    for path in (REPO/'third_party').rglob('*'):
        if path.is_file():copy(path,DEST/'source'/path.relative_to(REPO))
    for name in ['README.md','PARITY.md','THIRD-PARTY-NOTICES.txt','AGENTS.md']:copy(REPO/name,DEST/'source'/name)
    save(DEST/'manifest.json',{'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cyclesRevision':REVISION,
        'scope':validation['scope'],'driverSha256':validation['driverSha256'],
        'savedSha256':{str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}})
    print(DEST)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['run','archive']);args=parser.parse_args()
    if args.action=='run':run()
    else:archive()
