#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Original Cycles megakernel vs a limited WGSL multi-bounce integration."""
import argparse
import array
import json
import math
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; REPO=ROOT.parent; WORK=ROOT/'work/path'
sys.path.insert(0,str(REPO/'research')); sys.path.insert(0,str(REPO/'prototype/tools'))
import native_runner
from audit_webgpu import audit
from verify import png

def floats(path):
    result=array.array('f'); result.frombytes(path.read_bytes()); return result

def preview(directory,name,pixels,width,height):
    data=bytearray()
    for i,value in enumerate(pixels):
        if i%4==3: continue
        value=max(0,min(1,value)); value=value*12.92 if value<=.0031308 else 1.055*value**(1/2.4)-.055
        data.append(round(value*255))
    # Cycles film uses a lower-left origin; preview files use a top-left origin.
    rows=[data[y*width*3:(y+1)*width*3] for y in range(height)]
    (directory/'render.ppm').write_bytes(f'P6\n{width} {height}\n255\n'.encode()+b''.join(reversed(rows)))
    png(directory)
    if name!='render': (directory/'render.png').replace(directory/(name+'.png'))

def prepare(host_args=()):
    WORK.mkdir(parents=True,exist_ok=True)
    import material_gate
    material_gate.prepare()
    with (WORK/'build.log').open('w') as log:
        subprocess.run(['cmake','--build',str(REPO/'prototype/work/cycles-build'),'--target',
            'codebrix_cycles_path_host','--parallel','4'],stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([str(REPO/'prototype/work/cycles-build/bin/codebrix_cycles_path_host'),str(WORK),*host_args],
        text=True,capture_output=True)
    (WORK/'host.stderr.txt').write_text(result.stderr)
    if result.returncode: raise RuntimeError(result.stderr)
    (WORK/'host.json').write_text(result.stdout); meta=json.loads(result.stdout)
    materials=json.loads((ROOT/'work/material-host.json').read_text())
    constants=dict(materials['opcodes']); constants.update({'DIFFUSE':materials['diffuseId'],
        'OREN_NAYAR':materials['orenNayarId'],'TRANSLUCENT':materials['translucentId'],
        'REFLECT_DIFFUSE':materials['reflectDiffuseLabel'],'TRANSMIT_DIFFUSE':materials['transmitDiffuseLabel'],
        'MAX_BOUNCES':meta['bounces'],'OUTPUT_ROWS':meta['outputRows']})
    shader='\n'.join(f'const {k}: u32={v}u;' for k,v in constants.items())+'\n'
    render=(REPO/'prototype/shaders/render.wgsl').read_text()
    shader+=(REPO/'research/work/translated.wgsl').read_text()+'\n'
    shader+=render[:render.index('// Original prototype camera/shading stages:')]
    shader+=render[render.index('fn scene_float'):render.index('fn pixel_valid')]
    shader+='\n'.join((ROOT/'shaders'/name).read_text() for name in ['closures.wgsl','svm.wgsl','path_inputs.wgsl',
                                                                'lighting_none.wgsl','pathtrace.wgsl'])
    (WORK/'pathtrace.wgsl').write_text(shader)
    paths=meta['width']*meta['height']*meta['samples']
    (WORK/'cases.json').write_text(json.dumps([{'Name':'pathtrace','Shader':'pathtrace.wgsl',
        'Buffers':['pathtrace.input.bin','pathtrace.output.bin','pathtrace.scene.bin'],'Output':1,
        'Dispatches':[{'Entry':'main','Groups':(paths+63)//64}]}],indent=2)+'\n')
    preview(WORK,'reference',floats(WORK/'reference.rgba.bin'),meta['width'],meta['height'])
    print(result.stdout)

def verify(directory):
    meta=json.loads((WORK/'host.json').read_text()); width=meta['width']; height=meta['height']; samples=meta['samples']
    actual=floats(directory/'pathtrace.gpu.bin'); expected=floats(WORK/'pathtrace.expected.bin')
    hit_end=(meta['bounces']+3)*4; stride=meta.get('outputRows',meta['bounces']+3)*4; paths=width*height*samples
    if len(actual)!=len(expected) or len(actual)!=stride*paths: raise RuntimeError('Wrong result size')
    pixels=array.array('f',[0])*width*height*4
    path_failures=[]; path_color_errors=0; intersection_errors=0; first_intersection_errors=[]
    bounce_errors=0; decode_errors=0; primary_errors=0; trace_errors=0
    max_path_error=0; energy=[0.0,0.0]; indirect=[0.0,0.0]
    for i in range(paths):
        a=actual[i*stride:(i+1)*stride]; e=expected[i*stride:(i+1)*stride]
        delta=max(abs(a[c]-e[c]) for c in range(3)); max_path_error=max(max_path_error,delta)
        if any(not math.isfinite(a[c]) or abs(a[c]-e[c])>5e-5+2e-4*abs(e[c]) for c in range(4)):
            path_color_errors+=1
            if len(path_failures)<20: path_failures.append({'path':i,'actual':list(a[:4]),'expected':list(e[:4]),
                'gpuBounces':a[4],'cpuBounces':e[4],'gpuPrimitives':list(a[11:hit_end:4]),'cpuPrimitives':list(e[11:hit_end:4])})
        bounce_errors+=a[4]!=e[4]; decode_errors+=a[5]!=0; primary_errors+=a[11]!=e[11]
        trace_errors+=any(a[j]!=e[j] for j in range(11,hit_end,4))
        for j in range(11,hit_end,4):
            if a[j]==e[j] and e[j]>=0:
                bad=any(not math.isfinite(a[c]) or abs(a[c]-e[c])>5e-5+2e-4*abs(e[c]) for c in range(j-3,j))
                intersection_errors+=bad
                if bad and len(first_intersection_errors)<10:
                    first_intersection_errors.append({'path':i,'bounce':(j-11)//4,
                        'actualDistanceUvPrimitive':list(a[j-3:j+1]),'expectedDistanceUvPrimitive':list(e[j-3:j+1])})
        for k,values in enumerate([a,e]):
            energy[k]+=sum(values[:3]); indirect[k]+=sum(values[:3]) if values[4]>=2 else 0
        p=(i%(width*height))*4
        for c in range(4): pixels[p+c]+=a[c]/samples
    reference=floats(WORK/'reference.rgba.bin')
    errors=[i for i,(a,e) in enumerate(zip(pixels,reference)) if not math.isfinite(a) or abs(a-e)>5e-5+2e-4*abs(e)]
    image_passed=not errors; state_passed=not bounce_errors and not trace_errors and not decode_errors
    report={'passed':image_passed and state_passed and not path_color_errors and not intersection_errors,
        'imagePassed':image_passed,'pathStatePassed':state_passed,'pathColorMismatches':path_color_errors,
        'intersectionFloatMismatches':intersection_errors,
        'reference':'Original Cycles integrator_megakernel, original SVM/BSDFs and film',
        'width':width,'height':height,'samples':samples,'bounces':meta['bounces'],'paths':paths,
        'sharedNativeCameraAndSampler':meta['sharedNativeCameraAndSampler'],'absoluteTolerance':5e-5,'relativeTolerance':2e-4,
        'nextEventEstimation':meta['nextEventEstimation'],'multipleImportanceSampling':meta.get('multipleImportanceSampling',False),
        'imageMismatchedComponents':len(errors),'imageMismatchedPixels':len(set(i//4 for i in errors)),
        'imageMaximumAbsoluteError':max(abs(a-e) for a,e in zip(pixels,reference)),
        'imageRootMeanSquareError':math.sqrt(sum((a-e)**2 for a,e in zip(pixels,reference))/len(pixels)),
        'maximumPathColorError':max_path_error,'primaryPrimitiveMismatches':primary_errors,
        'pathsWithPrimitiveSequenceMismatch':trace_errors,'bounceCountMismatches':bounce_errors,'decodeErrorPaths':decode_errors,
        'totalRgbEnergyGpuCpu':energy,'rgbEnergyFromTwoOrMoreBouncesGpuCpu':indirect,'firstPathErrors':path_failures,
        'firstIntersectionErrors':first_intersection_errors,
        'firstImageErrors':[{'component':i,'actual':pixels[i],'expected':reference[i]} for i in errors[:10]]}
    (directory/'render.rgba.bin').write_bytes(pixels.tobytes()); preview(directory,'render',pixels,width,height)
    (directory/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if not k.startswith('first')},indent=2))
    return report['passed']

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
