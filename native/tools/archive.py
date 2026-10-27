#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Freeze the native library gate without redistributing native dependencies."""
import datetime
import importlib.util
import json
from pathlib import Path
from build import REPO,ROOT,WORK,REVISION
gate_spec=importlib.util.spec_from_file_location('native_gate',Path(__file__).with_name('run.py'))
gate=importlib.util.module_from_spec(gate_spec);gate_spec.loader.exec_module(gate)
SCENE,TABLES,sha,save=gate.SCENE,gate.TABLES,gate.sha,gate.save
spec=importlib.util.spec_from_file_location('parity_archive',REPO/'parity/tools/archive.py')
parity=importlib.util.module_from_spec(spec);spec.loader.exec_module(parity)

def main():
    dest=ROOT/'evidence/batched-backend'
    if dest.exists():raise RuntimeError('Checkpoint already exists; choose a new checkpoint instead of overwriting evidence')
    folders={'linux-x64':WORK/'linux-x64','pixel-api33':WORK/'android-98271FFBA0048D',
             'hp-api33':WORK/'android-192.168.86.225_5555','samsung-api36':WORK/'android-R52W207XQ1V'}
    for folder in folders.values():
        if not json.loads((folder/'validation.json').read_text())['passed']:raise RuntimeError('Native library gate has not passed: '+str(folder))
    if not json.loads((WORK/'fault-validation.json').read_text())['passed']:raise RuntimeError('Fault checks have not passed')
    dest.mkdir(parents=True)
    for name in TABLES+['reference.rgba.bin','reference.png','host.json','reference.json','lights.json']:
        parity.copy(SCENE/name,dest/'scene'/name)
    for name in ['backend.wgsl','kernel-metadata.json','translation.json','codegen.json']:
        parity.copy(WORK/'generated'/name,dest/'generated'/name)
    for name in ['webgpu-compatibility.json','fault-validation.json']:parity.copy(WORK/name,dest/name)
    binaries={}
    for rid in ['linux-x64','android-arm64','android-x64']:
        parity.copy(WORK/rid/'build.json',dest/'build'/rid/'build.json')
        binaries[rid]={name:sha(WORK/rid/name) for name in ['backend_driver','libcodebrix_cycles_webgpu.so','libcodebrix_wgpu.so']}
    for stage in ['main','shadow_resolve','accumulate']:
        folder=WORK/'linux-x64'/('fault-'+stage)
        for name in ['build.json','fault.json','fault.stderr.txt','backend.wgsl']:
            parity.copy(folder/name,dest/'faults'/stage/name)
        binaries['fault-'+stage]={name:sha(folder/name) for name in ['backend_driver','libcodebrix_cycles_webgpu.so','libcodebrix_wgpu.so']}
    for label,folder in folders.items():
        for name in ['render.json','render.rgba.bin','render.png','render.adapter.txt','render.library.txt',
                     'render.stderr.txt','webgpu-deployment.json','validation.json']:
            parity.copy(folder/name,dest/label/name)
        if (folder/'device.json').exists():parity.copy(folder/'device.json',dest/label/'device.json')
    parity.snapshot(dest)
    for path in ROOT.rglob('*'):
        if not path.is_file() or any(part in ('work','evidence','__pycache__') for part in path.relative_to(ROOT).parts):continue
        parity.copy(path,dest/'source/native'/path.relative_to(ROOT))
    for path in [REPO/'README.md',REPO/'docs/PARITY.md',REPO/'parity/README.md']:
        parity.copy(path,dest/'source'/path.relative_to(REPO))
    regressions=WORK/'regressions'
    for path in regressions.rglob('*'):
        if path.is_file():parity.copy(path,dest/'regressions'/path.relative_to(regressions))
    manifest={'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'scope':'Limited batched native backend; full Cycles functionality remains incomplete',
        'cyclesRevision':REVISION,'webGpuPackage':'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210',
        'binarySha256':binaries,
        'savedSha256':{str(path.relative_to(dest)):sha(path) for path in sorted(dest.rglob('*')) if path.is_file()}}
    save(dest/'manifest.json',manifest);print(dest)
if __name__=='__main__':main()
