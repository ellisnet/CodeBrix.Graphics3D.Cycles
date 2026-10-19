#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Gltf2 test import -> original native Cycles scene -> native WebGPU clay image."""
import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
import struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; REPO=ROOT.parent
WORK=ROOT/'work/assets'
sys.path.insert(0,str(REPO/'prototype/tools'))
spec=importlib.util.spec_from_file_location('prototype_runner',REPO/'prototype/tools/run.py')
prototype=importlib.util.module_from_spec(spec); spec.loader.exec_module(prototype)

def run(command,**kwargs):
    return subprocess.run(list(map(str,command)),check=True,**kwargs)

def save_run(command,directory,name,**kwargs):
    result=run(command,capture_output=True,text=True,**kwargs)
    (directory/(name+'.json')).write_text(result.stdout)
    (directory/(name+'.stderr.txt')).write_text(result.stderr)
    return result

def prepare(asset,directory):
    directory.mkdir(parents=True,exist_ok=True)
    run(['dotnet','build',ROOT/'GltfImport','-c','Release','--nologo','--no-restore'])
    run(['dotnet',ROOT/'GltfImport/bin/Release/net10.0/GltfImport.dll',asset,directory/'triangles.bin'])
    run(['cmake','--build',REPO/'prototype/work/cycles-build','--target','codebrix_cycles_host_probe','--parallel','4'])
    save_run([REPO/'prototype/work/cycles-build/bin/codebrix_cycles_host_probe',directory/'scene.bin',directory/'triangles.bin'],directory,'scene')
    upstream=Path.home()/'ClaudeHome/cycles-webgpu-upstream'
    run(['clang++','-std=c++20','-O2','-ffp-contract=off',REPO/'prototype/native/render_reference.cpp',
         '-I',upstream/'src','-I',REPO/'research/work','-o',directory/'reference'])
    save_run([directory/'reference',directory/'scene.bin',directory/'reference.rgba.bin'],directory,'reference')

def linux(directory):
    output=directory/'linux-x64'; output.mkdir(exist_ok=True)
    build=prototype.build('linux-x64')
    save_run([build/'render_driver',directory/'scene.bin',output/'render'],output,'render',timeout=180)
    loaded=Path((output/'render.library.txt').read_text().strip()).resolve()
    expected=prototype.PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    if loaded!=(build/'libcodebrix_wgpu.so').resolve() or loaded.read_bytes()!=expected.read_bytes():
        raise RuntimeError('Loaded WebGPU does not match the unchanged package binary')
    (output/'webgpu-deployment.json').write_text(json.dumps({'loadedPath':str(loaded),
        'sha256':hashlib.sha256(loaded.read_bytes()).hexdigest(),'matchesPinnedPackage':True},indent=2)+'\n')
    return prototype.verify(output,directory/'reference.rgba.bin')

def android(directory,serial):
    def adb(*args): return run([prototype.ADB,'-s',serial,*args],capture_output=True,text=True,timeout=180)
    def prop(key): return adb('shell','getprop',key).stdout.strip()
    api=int(prop('ro.build.version.sdk')); abi=prop('ro.product.cpu.abi')
    if api<33: raise RuntimeError('Device is below API 33')
    rid={'arm64-v8a':'android-arm64','x86_64':'android-x64'}[abi]
    build=prototype.build(rid)
    output=directory/('android-'+serial.replace(':','_')); output.mkdir(exist_ok=True)
    dest='/data/local/tmp/codebrix-cycles-asset-gate'
    adb('shell','mkdir','-p',dest)
    for path in [build/'render_driver',build/'libcodebrix_cycles_webgpu.so',build/'libcodebrix_wgpu.so',directory/'scene.bin']:
        adb('push',path,dest+'/')
    adb('shell','chmod','755',dest+'/render_driver')
    result=adb('shell',dest+'/render_driver',dest+'/scene.bin',dest+'/render')
    for name in ['render.rgba.bin','render.ppm','render.adapter.txt','render.library.txt']:
        adb('pull',dest+'/'+name,output)
    (output/'render.json').write_text(result.stdout); (output/'render.stderr.txt').write_text(result.stderr)
    loaded=(output/'render.library.txt').read_text().strip()
    expected=hashlib.sha256((build/'libcodebrix_wgpu.so').read_bytes()).hexdigest()
    if loaded!=dest+'/libcodebrix_wgpu.so' or adb('shell','sha256sum',loaded).stdout.split()[0]!=expected:
        raise RuntimeError('Device WebGPU does not match the unchanged package binary')
    (output/'webgpu-deployment.json').write_text(json.dumps({'loadedPath':loaded,'sha256':expected,
        'matchesPinnedPackage':True},indent=2)+'\n')
    (output/'device.json').write_text(json.dumps({'serial':serial,'api':api,'minimumApi':33,'abi':abi,
        'model':prop('ro.product.model'),'executionContext':'adb shell, not Android application sandbox',
        'scenePreparation':'Linux native Cycles host'},indent=2)+'\n')
    return prototype.verify(output,directory/'reference.rgba.bin')

def diagnose(directory,pixel):
    """Separate diagnostic dispatch; never substitutes for the image pass/fail."""
    import native_runner
    output=directory/('pixel-'+str(pixel)); output.mkdir(exist_ok=True)
    upstream=Path.home()/'ClaudeHome/cycles-webgpu-upstream'
    run(['clang++','-std=c++20','-O2','-ffp-contract=off',REPO/'prototype/native/render_reference.cpp',
         '-I',upstream/'src','-I',REPO/'research/work','-o',output/'reference'])
    save_run([output/'reference',directory/'scene.bin',output/'reference.rgba.bin',pixel,output/'pixel_trace'],output,'reference')
    render=(REPO/'prototype/shaders/render.wgsl').read_text()
    functions=render[render.index('fn scene_float'):render.index('@compute')]
    functions=functions.replace('fn pixel_valid(i: u32) -> bool { return i<config.image.x*config.image.y; }','')
    shader=(REPO/'research/work/translated.wgsl').read_text()+'\n'+functions+'\n'+(ROOT/'shaders/pixel_trace.wgsl').read_text()
    (output/'pixel_trace.wgsl').write_text('const SHARED_RAYS: bool=false;\n'+shader)
    (output/'pixel_trace_shared.wgsl').write_text('const SHARED_RAYS: bool=true;\n'+shader)
    (output/'pixel_trace.output.bin').write_bytes(bytes((output/'pixel_trace.expected.bin').stat().st_size))
    (output/'scene.bin').write_bytes((directory/'scene.bin').read_bytes())
    (output/'cases.json').write_text(json.dumps([{'Name':name,'Shader':name+'.wgsl',
        'Buffers':['pixel_trace.input.bin','pixel_trace.output.bin','scene.bin'],'Output':1,
        'Dispatches':[{'Entry':'main','Groups':1}]} for name in ('pixel_trace','pixel_trace_shared')],indent=2)+'\n')
    (output/'webgpu-compatibility.json').write_text(json.dumps(prototype.audit(),indent=2)+'\n')
    native_runner.linux(REPO/'research',output)
    def floats(path):
        data=path.read_bytes(); return struct.unpack('<'+'f'*(len(data)//4),data)
    a=floats(output/'pixel_trace.gpu.bin'); e=floats(output/'pixel_trace.expected.bin')
    shared=floats(output/'pixel_trace_shared.gpu.bin')
    samples=[]
    for i in range(len(e)//16):
        base=i*16
        samples.append({'sample':i,'cpuDirection':e[base:base+3],'gpuDirection':a[base:base+3],
            'cpuDistance':e[base+3],'gpuDistance':a[base+3],
            'cpuPrimitive':e[base+7],'gpuPrimitive':a[base+7],
            'sharedRayGpuPrimitive':shared[base+7],
            'cpuShadowPrimitive':e[base+10],'gpuShadowPrimitive':a[base+10],
            'cpuColor':e[base+12:base+15],'gpuColor':a[base+12:base+15],
            'sharedRayGpuColor':shared[base+12:base+15]})
    report={'pixel':pixel,'x':pixel%256,'y':pixel//256,
        'diagnosticOnly':'Separate dispatch; original image validation remains authoritative',
        'samples':samples}
    (output/'diagnostic.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('action',choices=['prepare','linux','android'])
    parser.add_argument('--asset',type=Path); parser.add_argument('--name',required=True); parser.add_argument('--serial')
    parser.add_argument('--trace-pixel',type=int)
    args=parser.parse_args()
    if not args.name or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-_' for c in args.name):
        parser.error('--name must contain lowercase letters, digits, hyphens or underscores')
    directory=WORK/args.name
    if args.trace_pixel is not None:
        if args.action!='linux' or not 0<=args.trace_pixel<256*192: parser.error('Pixel trace requires linux and a valid pixel index')
        diagnose(directory,args.trace_pixel); return
    if args.action=='prepare':
        if not args.asset: parser.error('--asset is required')
        prepare(args.asset.resolve(),directory); return
    if args.action=='linux': passed=linux(directory)
    else:
        if not args.serial: parser.error('--serial is required')
        passed=android(directory,args.serial)
    raise SystemExit(0 if passed else 1)
if __name__=='__main__': main()
