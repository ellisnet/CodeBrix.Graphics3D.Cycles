#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Run/archive the registered Cycles device and wavefront-control gate."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import subprocess
from pathlib import Path
from build import ROOT,REPO,WORK,REVISION,PACKAGE
from build_cycles import UPSTREAM
OUT=WORK/'device-queue/linux-x64'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,data):path.write_text(json.dumps(data,indent=2)+'\n')
def run():
    OUT.mkdir(parents=True,exist_ok=True)
    with (OUT/'build.log').open('w') as log:
        subprocess.run(['python3',str(ROOT/'tools/build_cycles.py')],stdout=log,stderr=subprocess.STDOUT,check=True)
    executable=WORK/'cycles-build/bin/codebrix_cycles_device_gate'
    result=subprocess.run([str(executable),str(OUT/'run')],capture_output=True,text=True,timeout=180)
    (OUT/'run.stdout.txt').write_text(result.stdout);(OUT/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    report=json.loads(result.stdout)
    required=['passed','registeredCyclesDevice','originalGpuSchedulerReset','unsupportedKernelRejected',
              'growthPreservesGpuWrites','uploadOrderingPassed','partialReadbackPassed']
    if not all(report[k] for k in required) or report['cyclesAllocationBytesAfterCleanup']!=0:raise RuntimeError('Device gate failed')
    # selectionCases 14 and sortCases 12 = the original 7 and 6 run twice, with
    # integrator.transparent_shadows false and then true (gate.cpp); the earlier
    # expectation of 7 and 6 predated that repetition.
    expected={'preparedSceneTables':11,'resetCases':7,'selectionCases':14,'sortCases':12,'compactionCases':2,
              'nativeStateFields':123,'states':4099,'invalidCasesRejected':7}
    if any(report[k]!=v for k,v in expected.items()):raise RuntimeError('Unexpected gate coverage')
    loaded=Path((OUT/'run.library.txt').read_text()).resolve()
    package=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    if loaded!=package.resolve() or sha(loaded)!=sha(package):raise RuntimeError('Loaded WebGPU differs from pinned package')
    tests=subprocess.run(['python3',str(ROOT/'tests/test_webgpu_audit.py')],capture_output=True,text=True,timeout=60)
    (OUT/'audit-tests.txt').write_text(tests.stdout+tests.stderr)
    if tests.returncode:raise RuntimeError('WebGPU compatibility-audit tests failed: '+tests.stderr)
    save(OUT/'audit-tests.json',{'passed':True,'scriptSha256':sha(ROOT/'tests/test_webgpu_audit.py'),'cases':6})
    save(OUT/'validation.json',{'passed':True,'scope':'Registered Cycles device, scene uploads and wavefront control (control gate only)',
        'results':report,'loadedWebGpuPath':str(loaded),'webGpuSha256':sha(loaded),'matchesPinnedPackage':True,
        'driverSha256':sha(executable),'shaderSha256':sha(ROOT/'shaders/controls.wgsl'),'cyclesRevision':REVISION})
    print(json.dumps(report,indent=2))

def archive():
    validation=json.loads((OUT/'validation.json').read_text())
    if not validation['passed'] or validation['shaderSha256']!=sha(ROOT/'shaders/controls.wgsl'):raise RuntimeError('Missing/stale successful gate')
    tests=json.loads((OUT/'audit-tests.json').read_text())
    if not tests['passed'] or tests['scriptSha256']!=sha(ROOT/'tests/test_webgpu_audit.py'):raise RuntimeError('Missing/stale successful audit tests')
    dest=ROOT/'evidence/device-queue'
    if dest.exists():raise RuntimeError('Checkpoint already exists; do not overwrite frozen evidence')
    spec=importlib.util.spec_from_file_location('parity_archive',REPO/'parity/tools/archive.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    copy=module.copy;dest.mkdir(parents=True)
    for path in OUT.iterdir():
        if path.is_file():copy(path,dest/'linux-x64'/path.name)
    for name in ['cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch']:copy(WORK/name,dest/name)
    for name in ['device/device.cpp','device/device.h','integrator/path_trace.cpp','state_copy_reference.inc']:
        copy(WORK/'cycles-overlay'/name,dest/'generated'/name)
    for name in ['kernel/device/gpu/parallel_prefix_sum.h','kernel/device/gpu/parallel_sorted_index.h',
                 'kernel/integrator/state_template.h','kernel/integrator/shadow_state_template.h']:
        copy(UPSTREAM/'src'/name,dest/'original-reference'/name)
    compile_commands=json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(dest/'compile-commands.json',[c for c in compile_commands if str(ROOT) in c['file'] or c['file'].endswith('/path_trace_work_gpu.cpp')])
    module.snapshot(dest)
    for path in ROOT.rglob('*'):
        if path.is_file() and not any(p in ('work','evidence','__pycache__') for p in path.relative_to(ROOT).parts):copy(path,dest/'source/native'/path.relative_to(ROOT))
    for name in ['README.md','docs/PARITY.md']:copy(REPO/name,dest/'source'/Path(name).name)
    save(dest/'manifest.json',{'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cyclesRevision':REVISION,
        'scope':validation['scope'],'driverSha256':validation['driverSha256'],
        'savedSha256':{str(p.relative_to(dest)):sha(p) for p in sorted(dest.rglob('*')) if p.is_file()}})
    print(dest)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['run','archive']);args=parser.parse_args()
    if args.action=='run':run()
    else:archive()
