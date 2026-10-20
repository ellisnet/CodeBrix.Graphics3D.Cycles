#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native scene-to-image gate. No CLR is involved in the renderer or tests."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from cycles_source import verify as verify_cycles
ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'work'
sys.path.insert(0,str(ROOT.parent/'research'))
from native_runner import PACKAGE, ADB, NDK
from audit_webgpu import audit
from verify import verify

def run(command, **kwargs):
    return subprocess.run(list(map(str,command)),check=True,**kwargs)

def build(rid):
    compatibility=audit()
    (WORK/'webgpu-compatibility.json').write_text(json.dumps(compatibility,indent=2)+'\n')
    run([sys.executable,ROOT/'tools/embed_shader.py'],capture_output=True,text=True)
    out=WORK/rid; out.mkdir(exist_ok=True)
    compiler={'linux-x64':'clang++','android-arm64':NDK/'aarch64-linux-android33-clang++',
              'android-x64':NDK/'x86_64-linux-android33-clang++'}[rid]
    dependency=PACKAGE/'runtimes'/rid/'native'
    common=[compiler,'-std=c++20','-O2','-I',ROOT/'native','-I',PACKAGE/'include',
            '-I',ROOT.parent/'third_party/deps/tbb/include','-Wl,-rpath,$ORIGIN']
    if rid.startswith('android'): common+=['-static-libstdc++']
    shared=out/'libcodebrix_cycles_webgpu.so'
    cmd=common+['-fPIC','-shared','-fvisibility=hidden',ROOT/'native/webgpu_backend.cpp',
        '-I',WORK/'generated','-I',PACKAGE/'include','-L',dependency,'-lcodebrix_wgpu','-o',shared]
    run(cmd)
    exe=out/'render_driver'
    cmd2=common+[ROOT/'native/render_driver.cpp','-L',out,'-lcodebrix_cycles_webgpu',
        '-L',dependency,'-lcodebrix_wgpu','-ldl','-o',exe]
    run(cmd2)
    (out/'build.json').write_text(json.dumps({'commands':[list(map(str,cmd)),list(map(str,cmd2))],
        'webGpuSha256':hashlib.sha256((dependency/'libcodebrix_wgpu.so').read_bytes()).hexdigest()},indent=2)+'\n')
    import shutil
    shutil.copy2(dependency/'libcodebrix_wgpu.so',out/'libcodebrix_wgpu.so')
    return out

def linux():
    out=build('linux-x64')
    host=run([WORK/'cycles-build/bin/codebrix_cycles_host_probe',WORK/'scene.bin'],capture_output=True,text=True)
    (WORK/'scene.json').write_text(host.stdout); (WORK/'scene.stderr.txt').write_text(host.stderr)
    upstream=verify_cycles()
    reference_command=['clang++','-std=c++20','-O2','-ffp-contract=off',ROOT/'native/render_reference.cpp',
        '-I',upstream/'src','-I',ROOT.parent/'third_party/deps/tbb/include',
        '-I',ROOT.parent/'research/work','-o',WORK/'render_reference']
    run(reference_command)
    reference=run([WORK/'render_reference',WORK/'scene.bin',WORK/'reference.rgba.bin'],capture_output=True,text=True)
    (WORK/'reference.json').write_text(reference.stdout)
    (WORK/'reference-build.json').write_text(json.dumps(list(map(str,reference_command)),indent=2)+'\n')
    result=run([out/'render_driver',WORK/'scene.bin',out/'render'],capture_output=True,text=True,timeout=180)
    (out/'render.json').write_text(result.stdout); (out/'render.stderr.txt').write_text(result.stderr)
    loaded=Path((out/'render.library.txt').read_text().strip())
    if loaded.resolve()!=(out/'libcodebrix_wgpu.so').resolve(): raise RuntimeError('Unexpected loaded WebGPU: '+str(loaded))
    if not verify(out): raise RuntimeError('Image validation failed')
    print(result.stdout,result.stderr)
    return out

def android(serial):
    def adb(*args,**kwargs): return run([ADB,'-s',serial,*args],timeout=180,**kwargs)
    def prop(key): return adb('shell','getprop',key,capture_output=True,text=True).stdout.strip()
    api=int(prop('ro.build.version.sdk')); abi=prop('ro.product.cpu.abi')
    if api<33: raise RuntimeError('Device below API 33')
    rid={'arm64-v8a':'android-arm64','x86_64':'android-x64'}[abi]
    out=build(rid); resultdir=WORK/('android-'+serial.replace(':','_')); resultdir.mkdir(exist_ok=True)
    device={'serial':serial,'api':api,'abi':abi,'model':prop('ro.product.model'),
            'minimumApi':33,'executionContext':'adb shell native executable; app sandbox not tested',
            'scenePreparation':'Linux Cycles host; same packed snapshot on Android'}
    dest='/data/local/tmp/codebrix-cycles-native-prototype'
    adb('shell','mkdir','-p',dest)
    for path in [out/'render_driver',out/'libcodebrix_cycles_webgpu.so',out/'libcodebrix_wgpu.so',WORK/'scene.bin']:
        adb('push',path,dest+'/',capture_output=True)
    adb('shell','chmod','755',dest+'/render_driver')
    result=adb('shell',dest+'/render_driver',dest+'/scene.bin',dest+'/render',capture_output=True,text=True)
    for name in ['render.rgba.bin','render.ppm','render.adapter.txt','render.library.txt']:
        adb('pull',dest+'/'+name,resultdir,capture_output=True)
    (resultdir/'render.json').write_text(result.stdout); (resultdir/'render.stderr.txt').write_text(result.stderr)
    (resultdir/'device.json').write_text(json.dumps(device,indent=2)+'\n')
    loaded=(resultdir/'render.library.txt').read_text().strip()
    if loaded!=dest+'/libcodebrix_wgpu.so': raise RuntimeError('Unexpected loaded WebGPU: '+loaded)
    remote_hash=adb('shell','sha256sum',loaded,capture_output=True,text=True).stdout.split()[0]
    expected_hash=hashlib.sha256((out/'libcodebrix_wgpu.so').read_bytes()).hexdigest()
    if remote_hash!=expected_hash: raise RuntimeError('Device WebGPU binary differs from package')
    (resultdir/'webgpu-deployment.json').write_text(json.dumps({'loadedPath':loaded,'sha256':remote_hash,
        'matchesPinnedPackage':True},indent=2)+'\n')
    if not verify(resultdir): raise RuntimeError('Image validation failed')
    print(result.stdout,result.stderr)
    return resultdir

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('action',choices=['build','linux','android'])
    parser.add_argument('--rid',default='linux-x64'); parser.add_argument('--serial')
    args=parser.parse_args()
    try:
        if args.action=='build': build(args.rid)
        elif args.action=='linux': linux()
        else: android(args.serial)
    except subprocess.CalledProcessError as e:
        print(e.stdout or '',e.stderr or '',file=sys.stderr); raise
