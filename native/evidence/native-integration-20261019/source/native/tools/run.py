#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Exercise the shared native backend on hardware and compare original Cycles film."""
import argparse
import hashlib
import json
import math
import shlex
import subprocess
import sys
from pathlib import Path
from build import build, REPO, WORK, PACKAGE
sys.path.insert(0,str(REPO/'parity/tools'))
from path_gate import floats, preview
from native_runner import ADB
SCENE=REPO/'parity/work/lighting'
TABLES=['pathtrace.scene.bin','camera.scene.bin','lights.scene.bin']

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,data): path.write_text(json.dumps(data,indent=2)+'\n')
def capture(command,folder,name='render',timeout=180):
    result=subprocess.run(command,text=True,capture_output=True,timeout=timeout)
    (folder/(name+'.stderr.txt')).write_text(result.stderr)
    (folder/(name+'.stdout.txt')).write_text(result.stdout)
    if result.returncode:raise RuntimeError(f'{name} exited {result.returncode}: {result.stderr}')
    report=json.loads(result.stdout);save(folder/(name+'.json'),report);return report

def verify(folder,scene=SCENE):
    report=json.loads((folder/'render.json').read_text())
    actual=floats(folder/'render.rgba.bin');expected=floats(scene/'reference.rgba.bin')
    if len(actual)!=len(expected):raise RuntimeError('Unexpected film size')
    errors=[i for i,(a,e) in enumerate(zip(actual,expected)) if not math.isfinite(a) or not math.isfinite(e) or abs(a-e)>5e-5+2e-4*abs(e)]
    prior_label={'linux-x64':'linux-x64','android-98271FFBA0048D':'pixel-api33',
                 'android-192.168.86.225_5555':'hp-api33','android-R52W207XQ1V':'samsung-api36'}.get(folder.name)
    prior=REPO/'parity/evidence/finite-light-mis'/str(prior_label)/'render.rgba.bin'
    lifecycle=all(report[k] for k in ['callerMemoryOwnershipPassed','repeatExact','failedScenePreservesResources',
                                     'replacementAndRestoreExact','batchSizeExact']) and report['invalidCasesRejected']==9
    deployment=json.loads((folder/'webgpu-deployment.json').read_text())
    result={'passed':not errors and lifecycle and deployment['matchesPinnedPackage'],
        'scope':'Limited native library image/lifecycle gate; not full Cycles parity',
        'originalCyclesMegakernelReference':True,'imageMismatchedComponents':len(errors),
        'maximumAbsoluteError':max(abs(a-e) for a,e in zip(actual,expected)),
        'absoluteTolerance':5e-5,'relativeTolerance':2e-4,'lifecyclePassed':lifecycle,
        'unchangedWebGpuPackage':deployment['matchesPinnedPackage'],
        'byteIdenticalToPreviousGpuImage':prior.exists() and prior.read_bytes()==(folder/'render.rgba.bin').read_bytes(),
        'inputSha256':{name:sha(scene/name) for name in TABLES+['reference.rgba.bin']},
        'outputSha256':sha(folder/'render.rgba.bin'),
        'firstErrors':[{'component':i,'actual':actual[i],'expected':expected[i]} for i in errors[:20]]}
    save(folder/'validation.json',result)
    if all(math.isfinite(x) for x in actual):preview(folder,'render',actual,report['first']['width'],report['first']['height'])
    return result

def linux(scene):
    out=build('linux-x64');capture([str(out/'backend_driver'),str(scene),str(out/'render')],out)
    loaded=Path((out/'render.library.txt').read_text()).resolve()
    package=PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    if loaded!=(out/'libcodebrix_wgpu.so').resolve() or sha(loaded)!=sha(package):
        raise RuntimeError('Loaded WebGPU differs from unchanged package')
    save(out/'webgpu-deployment.json',{'loadedPath':str(loaded),'sha256':sha(loaded),'matchesPinnedPackage':True})
    return out

def android(scene,serial):
    def adb(*args):return subprocess.check_output([str(ADB),'-s',serial,*map(str,args)],text=True,timeout=180)
    def prop(name):return adb('shell','getprop',name).strip()
    metadata={'serial':serial,'api':int(prop('ro.build.version.sdk')),'abi':prop('ro.product.cpu.abi'),
              'model':prop('ro.product.model'),'manufacturer':prop('ro.product.manufacturer'),
              'minimumApi':33,'executionContext':'adb shell shared native library; Android application sandbox and full Cycles host not tested'}
    if metadata['api']<33:raise RuntimeError('Device below API 33')
    rid={'arm64-v8a':'android-arm64','x86_64':'android-x64'}[metadata['abi']]
    binaries=build(rid);out=WORK/('android-'+serial.replace(':','_'));out.mkdir(exist_ok=True)
    save(out/'device.json',metadata)
    remote='/data/local/tmp/codebrix-cycles-native-backend'
    adb('shell','mkdir','-p',remote)
    names=['backend_driver','libcodebrix_cycles_webgpu.so','libcodebrix_wgpu.so']
    for file in [*[binaries/name for name in names],*[scene/name for name in TABLES]]:adb('push',file,remote+'/')
    adb('shell','chmod','755',remote+'/backend_driver')
    command=[remote+'/backend_driver',remote,remote+'/render']
    capture([str(ADB),'-s',serial,'shell',shlex.join(command)],out)
    for suffix in ['rgba.bin','adapter.txt','library.txt']:adb('pull',remote+'/render.'+suffix,out)
    loaded=(out/'render.library.txt').read_text()
    if loaded!=remote+'/libcodebrix_wgpu.so':raise RuntimeError('Unexpected loaded WebGPU path: '+loaded)
    hashes={name:adb('shell','sha256sum',remote+'/'+name).split()[0] for name in names}
    if any(hashes[name]!=sha(binaries/name) for name in names):raise RuntimeError('Deployed native bytes differ')
    package=PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
    if hashes['libcodebrix_wgpu.so']!=sha(package):raise RuntimeError('WebGPU differs from package')
    save(out/'webgpu-deployment.json',{'loadedPath':loaded,'sha256':hashes['libcodebrix_wgpu.so'],
                                     'matchesPinnedPackage':True,'deployedSha256':hashes})
    return out

def faults(scene):
    results={}
    for stage in ['main','shadow_resolve','accumulate']:
        out=build('linux-x64',stage)
        results[stage]=capture([str(out/'backend_driver'),str(scene),str(out/'render'),'--expect-render-failure'],out,'fault')
    save(WORK/'fault-validation.json',{'passed':all(r['missingStageDetected'] and r['callerOutputPreserved'] for r in results.values()),'stages':results})
    return results

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['linux','android','faults','verify'])
    p.add_argument('--serial');p.add_argument('--scene',type=Path,default=SCENE);p.add_argument('--results',type=Path)
    args=p.parse_args();scene=args.scene.resolve()
    if args.action=='faults':print(json.dumps(faults(scene),indent=2));return
    if args.action=='android':
        if not args.serial:p.error('--serial is required')
        folder=android(scene,args.serial)
    elif args.action=='linux':folder=linux(scene)
    else:
        if not args.results:p.error('--results is required')
        folder=args.results
    report=verify(folder,scene);print(json.dumps(report,indent=2))
    raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
