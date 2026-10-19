#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Save bounded, reviewable parity evidence without copying source model assets."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; REPO=ROOT.parent; WORK=ROOT/'work'

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def copy(source,dest):
    dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,dest)

def snapshot(dest):
    for top in ['parity','prototype','research']:
        for path in (REPO/top).rglob('*'):
            if not path.is_file() or any(x in path.relative_to(REPO).parts for x in ('work','evidence','bin','obj','__pycache__')): continue
            if path.suffix not in ('.py','.cpp','.h','.wgsl','.cmake','.cs','.csproj') and path.name!='CMakeLists.txt': continue
            copy(path,dest/'source'/path.relative_to(REPO))
    for source in [REPO/'LICENSE',ROOT/'THIRD-PARTY-NOTICES.txt',ROOT/'licenses/BSD-3-Clause.txt',
                   REPO/'research/THIRD-PARTY-NOTICES.txt']:
        copy(source,dest/'source'/source.relative_to(REPO))

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('gate',choices=['materials','assets','paths','camera']); args=parser.parse_args()
    dest=ROOT/'evidence'/{'materials':'material-closures','assets':'real-assets','paths':'multi-bounce',
                        'camera':'gpu-camera-sampling'}[args.gate]
    dest.mkdir(parents=True,exist_ok=True); inputs={}; outputs={}
    if args.gate in ('paths','camera'):
        work=WORK/('camera' if args.gate=='camera' else 'path')
        for name in ['pathtrace.wgsl','pathtrace.scene.bin','host.json','reference.json','reference.png',
                     'reference.rgba.bin','webgpu-compatibility.json','cases.json']:
            copy(work/name,dest/name)
        for path in work.glob('build-*.json'): copy(path,dest/path.name)
        for name in ['pathtrace.input.bin','pathtrace.expected.bin']:
            inputs[name]=sha(work/name)
        if args.gate=='camera':
            for name in ['camera.scene.bin','camera.json','camera.wgsl','sampler.wgsl']:
                copy(work/name,dest/name)
            for name in ['sampler.input.bin','sampler.expected.bin']: inputs[name]=sha(work/name)
        for label,folder in [('linux-x64',work),('pixel-api33',work/'android-98271FFBA0048D'),
                             ('hp-api33',work/'android-192.168.86.225_5555'),('samsung-api36',work/'android-R52W207XQ1V')]:
            for name in ['validation.json','native-gpu.json','webgpu-deployment.json','render.png','render.rgba.bin']:
                copy(folder/name,dest/label/name)
            if (folder/'device.json').exists(): copy(folder/'device.json',dest/label/'device.json')
            outputs[label+'/pathtrace.gpu.bin']=sha(folder/'pathtrace.gpu.bin')
            if args.gate=='camera':
                for name in ['sampler-validation.json','camera-validation.json']:
                    copy(folder/name,dest/label/name)
                for name in ['sampler.gpu.bin','camera.gpu.bin']: outputs[label+'/'+name]=sha(folder/name)
    elif args.gate=='materials':
        for name in ['material.wgsl','material-host.json','material.svm.bin','webgpu-compatibility.json','cases.json',
                     'svm_rejection.wgsl','svm-rejection-expected.json','svm_rejection.code.bin']:
            copy(WORK/name,dest/name)
        for path in WORK.glob('build-*.json'): copy(path,dest/path.name)
        for name in ['material.input.bin','material.expected.bin','svm_rejection.input.bin']:
            inputs[name]=sha(WORK/name)
        for label,folder in [('linux-x64',WORK),('pixel-api33',WORK/'android-98271FFBA0048D'),
                             ('hp-api33',WORK/'android-192.168.86.225_5555'),('samsung-api36',WORK/'android-R52W207XQ1V')]:
            for name in ['material-validation.json','svm-rejection-validation.json','native-gpu.json']:
                copy(folder/name,dest/label/name)
            if (folder/'device.json').exists(): copy(folder/'device.json',dest/label/'device.json')
            for name in ['material.gpu.bin','svm_rejection.gpu.bin']: outputs[label+'/'+name]=sha(folder/name)
    else:
        copy(REPO/'prototype/work/webgpu-compatibility.json',dest/'webgpu-compatibility.json')
        copy(REPO/'prototype/work/generated/render.wgsl',dest/'render.wgsl')
        for rid in ['linux-x64','android-arm64']:
            copy(REPO/'prototype/work'/rid/'build.json',dest/(rid+'-build.json'))
        for model in ['bench','lime','ceramic-home']:
            folder=WORK/'assets'/model
            for name in ['scene.json','reference.json','triangles.bin.json']: copy(folder/name,dest/model/name)
            for name in ['triangles.bin','scene.bin','reference.rgba.bin']: inputs[model+'/'+name]=sha(folder/name)
            for device in ['linux-x64','android-98271FFBA0048D']:
                for name in ['render.png','render.json','validation.json','render.adapter.txt','render.library.txt','webgpu-deployment.json']:
                    copy(folder/device/name,dest/model/device/name)
                if (folder/device/'device.json').exists(): copy(folder/device/'device.json',dest/model/device/'device.json')
                outputs[model+'/'+device+'/render.rgba.bin']=sha(folder/device/'render.rgba.bin')
            for diagnostic in folder.glob('pixel-*'):
                for name in ['diagnostic.json','native-gpu.json','pixel_trace.wgsl','pixel_trace_shared.wgsl',
                             'pixel_trace.input.bin','pixel_trace.expected.bin','pixel_trace.gpu.bin','pixel_trace_shared.gpu.bin']:
                    copy(diagnostic/name,dest/model/diagnostic.name/name)
    snapshot(dest)
    manifest={'cyclesRevision':'a456b761034dda42c32eef9f4aae0fa5a5c9f604',
        'webGpuPackage':'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433',
        'inputSha256':inputs,'outputSha256':outputs,
        'savedSha256':{str(p.relative_to(dest)):sha(p) for p in sorted(dest.rglob('*')) if p.is_file() and p.name!='manifest.json'}}
    (dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(dest)
if __name__=='__main__': main()
