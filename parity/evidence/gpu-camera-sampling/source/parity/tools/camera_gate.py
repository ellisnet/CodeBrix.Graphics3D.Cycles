#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""GPU camera/Sobol probes, then the original-megakernel image comparison."""
import argparse
import array
import json
import math
import struct
import sys
from pathlib import Path
import path_gate
ROOT=path_gate.ROOT; REPO=ROOT.parent; WORK=ROOT/'work/camera'
sys.path.insert(0,str(REPO/'research')); sys.path.insert(0,str(REPO/'prototype/tools'))
import native_runner
from audit_webgpu import audit

def rounding_diagnosis(actual,expected):
    """Explain differences; the native Cycles results remain the pass/fail oracle."""
    def u32s(path):
        a=array.array('I'); a.frombytes(path.read_bytes()); return a
    def r32(x): return struct.unpack('f',struct.pack('f',x))[0]
    queries=u32s(WORK/'sampler.input.bin'); query_f=path_gate.floats(WORK/'sampler.input.bin')
    reference=u32s(WORK/'sampler.expected.bin'); scene=u32s(WORK/'camera.scene.bin')
    scene_f=path_gate.floats(WORK/'camera.scene.bin'); base=scene[16]
    result={'method':'Float32 separately rounded and fused rotation predictions; diagnostic only',
            'rotatedComponents':0,'nativeEqualsSeparate':0,'gpuEqualsFused':0,
            'differentGpuNative':0,'differencesExplainedByFusedRotation':0}
    for i in range(0,len(actual),8):
        distance=query_f[i+5]
        if distance>=1: continue
        pixel=reference[i]; index=reference[i+1]; dimension=queries[i+3]
        for c,salt in enumerate([0,0xca0e1151,0xbf604c5a,0x99634d1d]):
            h=(dimension^61)^(pixel^salt); h=(h+(h<<3))&0xffffffff; h^=h>>4; h=(h*0x27d4eb2d)&0xffffffff
            product=r32(r32(h)*r32(1/4294967808)); value=scene_f[base+index*4+c]
            separate=r32(value+r32(product*distance)); separate=r32(separate-math.floor(separate))
            fused=r32(value+product*distance); fused=r32(fused-math.floor(fused))
            a=actual[i+4+c]; e=expected[i+4+c]
            result['rotatedComponents']+=1; result['nativeEqualsSeparate']+=e==separate
            result['gpuEqualsFused']+=a==fused; result['differentGpuNative']+=a!=e
            result['differencesExplainedByFusedRotation']+=a!=e and e==separate and a==fused
    return result

def prepare():
    path_gate.WORK=WORK; path_gate.prepare()
    meta=json.loads((WORK/'host.json').read_text()); meta['sharedNativeCameraAndSampler']=False
    (WORK/'host.json').write_text(json.dumps(meta,indent=2)+'\n')
    base=(ROOT/'shaders/sampling.wgsl').read_text()
    camera=(ROOT/'shaders/camera.wgsl').read_text()
    binding='@group(0) @binding(0) var<storage,read> camera_data: array<vec4<u32>>;\n'
    common=binding+base+camera
    path_shader=(WORK/'pathtrace.wgsl').read_text()
    path_shader=path_shader.replace((ROOT/'shaders/path_inputs.wgsl').read_text(),common)
    (WORK/'pathtrace.wgsl').write_text(path_shader)
    (WORK/'camera.wgsl').write_text(common+'''
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i>=path_count()) { return; }
    let bounces=camera_data[0].w; let base=i*(3u+bounces); let initial=path_initial(i);
    outputs[base]=initial.origin; outputs[base+1u]=initial.direction;
    outputs[base+2u]=vec4<f32>(initial.throughput,0);
    for(var b=0u;b<bounces;b++) { outputs[base+3u+b]=vec4<f32>(path_random(i,b),0); }
}
''')
    (WORK/'sampler.wgsl').write_text('''
@group(0) @binding(0) var<storage,read> queries: array<vec4<u32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<u32>>;
@group(0) @binding(2) var<storage,read> camera_data: array<vec4<u32>>;
'''+base+'''
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x*2u; if(i>=arrayLength(&queries)) { return; }
    let q=queries[i]; let seed=queries[i+1u].x; let distance=bitcast<f32>(queries[i+1u].y);
    let pixel=pixel_hash(q.x,q.y,seed);
    outputs[i]=vec4<u32>(pixel,sobol_index(q.z,q.w,select(pixel,seed,distance<1.0)),0,0);
    outputs[i+1u]=bitcast<vec4<u32>>(sobol_sample(q.z,pixel,q.w,seed,distance));
}
''')
    (WORK/'camera.output.bin').write_bytes(bytes((WORK/'pathtrace.input.bin').stat().st_size))
    # Keep original per-path native values as reference data only. Neither the
    # camera dispatch nor the rendering dispatch uploads this file to the GPU.
    cases=json.loads((WORK/'cases.json').read_text())
    cases[0]['Buffers'][0]='camera.scene.bin'
    paths=meta['width']*meta['height']*meta['samples']
    probe=json.loads((WORK/'camera.json').read_text())
    cases[:0]=[
        {'Name':'sampler','Shader':'sampler.wgsl','Buffers':['sampler.input.bin','sampler.output.bin','camera.scene.bin'],
         'Output':1,'Dispatches':[{'Entry':'main','Groups':(probe['samplerCases']+63)//64}]},
        {'Name':'camera','Shader':'camera.wgsl','Buffers':['camera.scene.bin','camera.output.bin'],
         'Output':1,'Dispatches':[{'Entry':'main','Groups':(paths+63)//64}]}]
    (WORK/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')

def verify(directory):
    def uints(path):
        a=array.array('I'); a.frombytes(path.read_bytes()); return a
    actual=uints(directory/'sampler.gpu.bin'); expected=uints(WORK/'sampler.expected.bin')
    if len(actual)!=len(expected): raise RuntimeError('Wrong sampler output size')
    integer_errors=[]; float_errors=[]; bit_errors=0; max_error=0
    af=path_gate.floats(directory/'sampler.gpu.bin'); ef=path_gate.floats(WORK/'sampler.expected.bin')
    for i,(a,e) in enumerate(zip(actual,expected)):
        if i%8<4:
            if a!=e: integer_errors.append(i)
        else:
            bit_errors+=a!=e; max_error=max(max_error,abs(af[i]-ef[i]))
            if not math.isfinite(af[i]) or abs(af[i]-ef[i])>1e-7+1e-7*abs(ef[i]): float_errors.append(i)
    sampler={'passed':not integer_errors and not float_errors,'cases':len(actual)//8,
        'integerMismatches':len(integer_errors),'floatMismatches':len(float_errors),'floatBitMismatches':bit_errors,
        'maximumAbsoluteError':max_error,'absoluteTolerance':1e-7,'relativeTolerance':1e-7,
        'firstErrors':[{'component':i,'actualBits':actual[i],'expectedBits':expected[i]} for i in (integer_errors+float_errors)[:10]]}
    sampler['roundingDiagnosis']=rounding_diagnosis(af,ef)
    (directory/'sampler-validation.json').write_text(json.dumps(sampler,indent=2)+'\n')
    meta=json.loads((WORK/'host.json').read_text()); stride=(meta['bounces']+3)*4
    actual=path_gate.floats(directory/'camera.gpu.bin'); expected=path_gate.floats(WORK/'pathtrace.input.bin')
    if len(actual)!=len(expected): raise RuntimeError('Wrong camera output size')
    ray_errors=[]; sample_errors=[]; max_ray=[0.0]*8
    for i,(a,e) in enumerate(zip(actual,expected)):
        c=i%stride
        if c<8:
            max_ray[c]=max(max_ray[c],abs(a-e))
            if not math.isfinite(a) or abs(a-e)>5e-5+2e-4*abs(e): ray_errors.append(i)
        elif a!=e: sample_errors.append(i)
    camera={'passed':not ray_errors and not sample_errors,'paths':len(actual)//stride,
        'rayComponentMismatches':len(ray_errors),'samplerOrThroughputExactMismatches':len(sample_errors),
        'maximumRayAbsoluteErrors':max_ray,'absoluteTolerance':5e-5,'relativeTolerance':2e-4,
        'firstErrors':[{'component':i,'actual':actual[i],'expected':expected[i]} for i in (ray_errors+sample_errors)[:10]]}
    (directory/'camera-validation.json').write_text(json.dumps(camera,indent=2)+'\n')
    path_gate.WORK=WORK; path_passed=path_gate.verify(directory)
    print(json.dumps({'sampler':sampler,'camera':camera},indent=2))
    return sampler['passed'] and camera['passed'] and path_passed

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('action',choices=['prepare','linux','android','verify'])
    parser.add_argument('--serial'); parser.add_argument('--results',type=Path); args=parser.parse_args()
    if args.action=='prepare': prepare(); return
    if args.action=='verify': raise SystemExit(0 if verify(args.results or WORK) else 1)
    (WORK/'webgpu-compatibility.json').write_text(json.dumps(audit(),indent=2)+'\n')
    if args.action=='linux': native_runner.linux(REPO/'research',WORK); directory=WORK
    else:
        if not args.serial: parser.error('--serial is required')
        directory=native_runner.android(REPO/'research',WORK,args.serial)
    raise SystemExit(0 if verify(directory) else 1)
if __name__=='__main__': main()
