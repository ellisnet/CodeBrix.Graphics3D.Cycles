#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Finite lights, NEE, opaque shadows and bidirectional MIS against original Cycles."""
import argparse
import json
import math
from pathlib import Path
import camera_gate
import path_gate
ROOT=path_gate.ROOT; REPO=ROOT.parent; WORK=ROOT/'work/lighting'
native_runner=camera_gate.native_runner

def prepare():
    camera_gate.WORK=WORK; camera_gate.prepare(['--lights'])
    source=(WORK/'pathtrace.wgsl').read_text()
    lighting=(ROOT/'shaders/light_sampling.wgsl').read_text()+'\n'+(ROOT/'shaders/lighting.wgsl').read_text()
    source=source.replace((ROOT/'shaders/lighting_none.wgsl').read_text(),lighting)
    (WORK/'pathtrace.wgsl').write_text(source)
    material=json.loads((ROOT/'work/material-host.json').read_text())
    constants={'DIFFUSE':material['diffuseId'],'OREN_NAYAR':material['orenNayarId'],
        'TRANSLUCENT':material['translucentId'],'REFLECT_DIFFUSE':material['reflectDiffuseLabel'],
        'TRANSMIT_DIFFUSE':material['transmitDiffuseLabel']}
    shader='\n'.join(f'const {k}: u32={v}u;' for k,v in constants.items())+'\n'
    shader+=(ROOT/'shaders/closures.wgsl').read_text()+(ROOT/'shaders/light_sampling.wgsl').read_text()+'''
@group(0) @binding(0) var<storage,read> queries: array<vec4<f32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> light_data: array<vec4<u32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i*3u>=arrayLength(&queries)) { return; }
    let p=queries[i*3u]; let n=queries[i*3u+1u]; let random=queries[i*3u+2u].xyz;
    let sample=sample_light(p.xyz,n.xyz,p.w!=0,random); let light=light_float(light_row(sample.id));
    let t=sphere_intersect(p.xyz,sample.d,0,3.4028234663852886e38,light);
    let delta=p.xyz-light.xyz;
    let pdf=sphere_pdf(dot(delta,delta),light.w*light.w,n.xyz,sample.d,p.w!=0)*light_float(1).x;
    outputs[i*5u]=vec4<f32>(sample.p,sample.t);
    outputs[i*5u+1u]=vec4<f32>(sample.d,sample.pdf);
    outputs[i*5u+2u]=vec4<f32>(sample.n,sample.eval_fac);
    outputs[i*5u+3u]=vec4<f32>(f32(sample.id),select(0.0,1.0,t>=0),max(t,0),pdf);
    outputs[i*5u+4u]=vec4<f32>(power_weight(sample.pdf,n.w),power_weight(n.w,pdf),light_float(1).x,0);
}
'''
    (WORK/'light_sample.wgsl').write_text(shader)
    cases=json.loads((WORK/'cases.json').read_text())
    path_case=next(c for c in cases if c['Name']=='pathtrace')
    path_case['Buffers']+=['lights.scene.bin','shadow_rays.bin']
    path_case['Dispatches'].append({'Entry':'shadow_resolve','Groups':path_case['Dispatches'][0]['Groups']})
    host=json.loads((WORK/'host.json').read_text())
    (WORK/'shadow_rays.bin').write_bytes(bytes(host['width']*host['height']*host['samples']*host['bounces']*32))
    meta=json.loads((WORK/'lights.json').read_text())
    cases.insert(0,{'Name':'light_sample','Shader':'light_sample.wgsl',
        'Buffers':['light_sample.input.bin','light_sample.output.bin','lights.scene.bin'],'Output':1,
        'Dispatches':[{'Entry':'main','Groups':(meta['samplerCases']+63)//64}]})
    (WORK/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')

def verify(directory):
    camera_gate.WORK=WORK; previous_passed=camera_gate.verify(directory)
    a=path_gate.floats(directory/'light_sample.gpu.bin'); e=path_gate.floats(WORK/'light_sample.expected.bin')
    if len(a)!=len(e): raise RuntimeError('Wrong light sample output size')
    bad=[]; discrete=[]
    for i,(av,ev) in enumerate(zip(a,e)):
        if i%20 in (12,13):
            if av!=ev: discrete.append(i)
        elif not math.isfinite(av) or abs(av-ev)>5e-5+2e-4*abs(ev): bad.append(i)
    probe={'passed':not bad and not discrete,'cases':len(a)//20,'floatMismatches':len(bad),
        'lightIdOrIntersectionMismatches':len(discrete),'maximumAbsoluteError':max(abs(av-ev) for av,ev in zip(a,e)),
        'firstErrors':[{'case':i//20,'component':i%20,'actual':a[i],'expected':e[i]} for i in (discrete+bad)[:20]],
        'absoluteTolerance':5e-5,'relativeTolerance':2e-4}
    queries=path_gate.floats(WORK/'light_sample.input.bin')
    def boundary(case): return queries[case*12+8] in (0,1) or queries[case*12+9] in (0,1)
    probe['lightSelectionMismatches']=sum(i%20==12 for i in discrete)
    probe['sphereIntersectionClassificationMismatches']=sum(i%20==13 for i in discrete)
    probe['boundaryCases']=sum(boundary(i) for i in range(len(a)//20))
    probe['boundaryScalarMismatches']=sum(boundary(i//20) for i in bad+discrete)
    probe['interiorScalarMismatches']=sum(not boundary(i//20) for i in bad+discrete)
    probe['firstInteriorErrors']=[{'case':i//20,'component':i%20,'actual':a[i],'expected':e[i]}
                                 for i in (discrete+bad) if not boundary(i//20)][:10]
    (directory/'light-sample-validation.json').write_text(json.dumps(probe,indent=2)+'\n')
    meta=json.loads((WORK/'host.json').read_text()); bounces=meta['bounces']; stride=meta['outputRows']*4
    a=path_gate.floats(directory/'pathtrace.gpu.bin'); e=path_gate.floats(WORK/'pathtrace.expected.bin')
    shadow_base=(bounces+3)*4; forward=(3*bounces+3)*4
    id_errors=visibility_errors=shadow_float_errors=forward_count_errors=forward_color_errors=0
    counts=[[0,0,0],[0,0,0]]; first=[]
    for p in range(len(a)//stride):
        base=p*stride
        for b in range(bounces):
            o=base+shadow_base+b*8
            id_errors+=a[o+4]!=e[o+4]; visibility_errors+=a[o+5]!=e[o+5]
            bad_float=any(not math.isfinite(a[o+c]) or abs(a[o+c]-e[o+c])>5e-5+2e-4*abs(e[o+c]) for c in range(4))
            shadow_float_errors+=bad_float
            if (bad_float or a[o+4]!=e[o+4] or a[o+5]!=e[o+5]) and len(first)<15:
                first.append({'path':p,'bounce':b,'gpuThroughputDistanceLightBlocked':list(a[o:o+6]),
                              'cpuThroughputDistanceLightBlocked':list(e[o:o+6])})
            for k,v in enumerate([a,e]):
                counts[k][0]+=v[o+4]>=0; counts[k][1]+=v[o+5]==1
        o=base+forward; forward_count_errors+=a[o+3]!=e[o+3]
        forward_color_errors+=any(not math.isfinite(a[o+c]) or abs(a[o+c]-e[o+c])>5e-5+2e-4*abs(e[o+c]) for c in range(3))
        counts[0][2]+=a[o+3]; counts[1][2]+=e[o+3]
    passed=not(id_errors or visibility_errors or shadow_float_errors or forward_count_errors or forward_color_errors)
    report={'passed':passed,'gpuDispatches':2,'cpuShadowOrLightingEvaluation':False,
        'shadowLightSelectionMismatches':id_errors,'shadowVisibilityMismatches':visibility_errors,
        'shadowThroughputOrDistanceMismatches':shadow_float_errors,'forwardLightCountMismatches':forward_count_errors,
        'forwardLightColorMismatches':forward_color_errors,'shadowRaysBlockedAndForwardHitsGpuCpu':counts,
        'absoluteTolerance':5e-5,'relativeTolerance':2e-4,'firstShadowErrors':first}
    (directory/'lighting-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    overall={'passed':previous_passed and passed and probe['passed'],
             'lightSamplesPassed':probe['passed'],'lightingPathsPassed':passed,
             'cameraSamplerAndRenderPassed':previous_passed}
    (directory/'gate-validation.json').write_text(json.dumps(overall,indent=2)+'\n')
    print(json.dumps({'lightSample':probe,'lighting':report,'overall':overall},indent=2))
    return overall['passed']

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('action',choices=['prepare','linux','android','verify'])
    parser.add_argument('--serial'); parser.add_argument('--results',type=Path); args=parser.parse_args()
    if args.action=='prepare': prepare(); return
    if args.action=='verify': raise SystemExit(0 if verify(args.results or WORK) else 1)
    (WORK/'webgpu-compatibility.json').write_text(json.dumps(camera_gate.audit(),indent=2)+'\n')
    if args.action=='linux': native_runner.linux(REPO/'research',WORK); directory=WORK
    else:
        if not args.serial: parser.error('--serial is required')
        directory=native_runner.android(REPO/'research',WORK,args.serial)
    raise SystemExit(0 if verify(directory) else 1)
if __name__=='__main__': main()
