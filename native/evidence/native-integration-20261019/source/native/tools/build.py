#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the native backend with embedded WGSL and the unchanged package ABI."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parent;WORK=ROOT/'work'
sys.path.insert(0,str(REPO/'research'));sys.path.insert(0,str(REPO/'prototype/tools'))
sys.path.insert(0,str(REPO/'tools'))
from cycles_source import SOURCE, verify as verify_cycles
from native_runner import PACKAGE,NDK
from audit_webgpu import audit
from translate import generate
REVISION='a456b761034dda42c32eef9f4aae0fa5a5c9f604'

def shaders():
    out=WORK/'generated';out.mkdir(parents=True,exist_ok=True)
    upstream=verify_cycles()
    # The original native host supplies enum values; this is build-time work,
    # not a runtime dependency of the shared GPU backend.
    host=REPO/'prototype/work/cycles-build/bin/codebrix_cycles_material_host'
    inputs=[Path(__file__),REPO/'research/translate.py',REPO/'research/native/reference.cpp',
            REPO/'prototype/shaders/render.wgsl',ROOT/'shaders/queue.wgsl',host,
            *sorted((REPO/'parity/shaders').glob('*.wgsl'))]
    fingerprint=hashlib.sha256(REVISION.encode()+b''.join(hashlib.sha256(p.read_bytes()).digest() for p in inputs)).hexdigest()
    cache=out/'codegen.json'
    if cache.exists():
        previous=json.loads(cache.read_text())
        if previous['fingerprint']==fingerprint and all((out/name).exists() and
            hashlib.sha256((out/name).read_bytes()).hexdigest()==digest for name,digest in previous['outputs'].items()):
            return out
    result=subprocess.run([str(host),str(out)],check=True,capture_output=True,text=True)
    (out/'kernel-metadata.json').write_text(result.stdout);meta=json.loads(result.stdout)
    constants=dict(meta['opcodes']);constants.update({'DIFFUSE':meta['diffuseId'],'OREN_NAYAR':meta['orenNayarId'],
        'TRANSLUCENT':meta['translucentId'],'REFLECT_DIFFUSE':meta['reflectDiffuseLabel'],
        'TRANSMIT_DIFFUSE':meta['transmitDiffuseLabel'],'MAX_BOUNCES':4,'OUTPUT_ROWS':16})
    # Native scene validation decodes this pinned subset too; catch drift.
    expected={'OP_END':0,'OP_JUMP':1,'OP_GEOMETRY':11,'OP_SET_WEIGHT':5,'OP_BSDF':2,'DIFFUSE':2,'TRANSLUCENT':9}
    if any(constants[k]!=v for k,v in expected.items()):raise RuntimeError('SVM validation constants require updating')
    shader='\n'.join(f'const {k}: u32={v}u;' for k,v in constants.items())+'\n'
    shader+=generate(upstream,out,REPO/'research/native/reference.cpp')+'\n'
    render=(REPO/'prototype/shaders/render.wgsl').read_text()
    shader+=render[:render.index('// Original prototype camera/shading stages:')]
    shader+=render[render.index('fn scene_float'):render.index('fn pixel_valid')]
    for name in ['closures','svm','sampling','camera','light_sampling','lighting','pathtrace']:
        shader+='\n'+(REPO/'parity/shaders'/f'{name}.wgsl').read_text()
    shader+='\n'+(ROOT/'shaders/queue.wgsl').read_text()
    if ')CBWGSL"' in shader:raise RuntimeError('Shader delimiter collision')
    (out/'backend.wgsl').write_text(shader)
    (out/'backend_shader.inc').write_text('R"CBWGSL('+shader+')CBWGSL"\n')
    cache.write_text(json.dumps({'fingerprint':fingerprint,'outputs':{name:hashlib.sha256((out/name).read_bytes()).hexdigest()
        for name in ['backend.wgsl','backend_shader.inc','kernel-metadata.json','translation.json']}},indent=2)+'\n')
    return out

def build(rid,fault_stage=None):
    WORK.mkdir(parents=True,exist_ok=True)
    (WORK/'webgpu-compatibility.json').write_text(json.dumps(audit(),indent=2)+'\n')
    generated=shaders();out=WORK/rid;out.mkdir(exist_ok=True)
    if fault_stage:
        if rid!='linux-x64' or fault_stage not in ('main','shadow_resolve','accumulate'):
            raise ValueError('Fault injection is a Linux test-only build')
        out=out/('fault-'+fault_stage);out.mkdir(exist_ok=True)
        source=(generated/'backend.wgsl').read_text()
        entry='fn '+fault_stage+'('
        if source.count(entry)!=1:raise RuntimeError('Expected exactly one entry point')
        source=source.replace(entry,'fn inactive_'+fault_stage+'(')
        source+='\n@compute @workgroup_size(64) fn '+fault_stage+'() {}\n'
        (out/'backend.wgsl').write_text(source)
        (out/'backend_shader.inc').write_text('R"CBWGSL('+source+')CBWGSL"\n')
        generated=out
    compiler={'linux-x64':'clang++','android-arm64':str(NDK/'aarch64-linux-android33-clang++'),
              'android-x64':str(NDK/'x86_64-linux-android33-clang++')}[rid]
    dependency=PACKAGE/'runtimes'/rid/'native'
    common=[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),
            '-I',str(PACKAGE/'include'),'-I',str(REPO/'third_party/deps/tbb/include'),'-Wl,-rpath,$ORIGIN']
    if rid.startswith('android'):common+=['-static-libstdc++']
    library=out/'libcodebrix_cycles_webgpu.so'
    command=common+['-fPIC','-shared','-fvisibility=hidden','-I',str(generated),
        *[str(ROOT/'src'/name) for name in ['webgpu_device.cpp','scene_data.cpp','backend.cpp']],
        '-L',str(dependency),'-lcodebrix_wgpu','-ldl','-o',str(library)]
    subprocess.run(command,check=True)
    driver=common+[str(ROOT/'tests/backend_driver.cpp'),'-L',str(out),'-lcodebrix_cycles_webgpu',
        '-L',str(dependency),'-lcodebrix_wgpu','-pthread','-o',str(out/'backend_driver')]
    subprocess.run(driver,check=True)
    shutil.copy2(dependency/'libcodebrix_wgpu.so',out/'libcodebrix_wgpu.so')
    (out/'build.json').write_text(json.dumps({'commands':[command,driver],'cyclesRevision':REVISION,
        'webGpuSha256':hashlib.sha256((dependency/'libcodebrix_wgpu.so').read_bytes()).hexdigest(),
        'shaderSha256':hashlib.sha256((generated/'backend.wgsl').read_bytes()).hexdigest(),
        'faultInjectionStage':fault_stage},indent=2)+'\n')
    return out
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--rid',default='linux-x64',choices=['linux-x64','android-arm64','android-x64'])
    p.add_argument('--fault-stage',choices=['main','shadow_resolve','accumulate'])
    args=p.parse_args();print(build(args.rid,args.fault_stage))
