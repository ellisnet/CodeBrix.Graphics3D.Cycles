#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Original Cycles SVM/BSDF behavior against typed WGSL on shipped WebGPU."""
import argparse
import array
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; WORK=ROOT/'work'; REPO=ROOT.parent
sys.path.insert(0,str(REPO/'research'))
sys.path.insert(0,str(REPO/'prototype/tools'))
import native_runner
import svm_rejection
from audit_webgpu import audit

def prepare():
    WORK.mkdir(exist_ok=True)
    with (WORK/'material-build.log').open('w') as log:
        subprocess.run(['cmake','--build',str(REPO/'prototype/work/cycles-build'),'--target',
            'codebrix_cycles_material_host','--parallel','4'],stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([str(REPO/'prototype/work/cycles-build/bin/codebrix_cycles_material_host'),str(WORK)],
        capture_output=True,text=True,check=True)
    (WORK/'material-host.json').write_text(result.stdout)
    (WORK/'material-host.stderr.txt').write_text(result.stderr)
    meta=json.loads(result.stdout)
    constants=dict(meta['opcodes']); constants.update({'DIFFUSE':meta['diffuseId'],'OREN_NAYAR':meta['orenNayarId'],
        'TRANSLUCENT':meta['translucentId'],'REFLECT_DIFFUSE':meta['reflectDiffuseLabel'],'TRANSMIT_DIFFUSE':meta['transmitDiffuseLabel']})
    shader='\n'.join(f'const {k}: u32={v}u;' for k,v in constants.items())+'\n'
    shader+='\n'.join((ROOT/'shaders'/name).read_text() for name in ['closures.wgsl','svm.wgsl','material.wgsl'])
    (WORK/'material.wgsl').write_text(shader)
    case={'Name':'material','Shader':'material.wgsl','Buffers':['material.input.bin','material.output.bin','material.svm.bin'],
          'Output':1,'Dispatches':[{'Entry':'main','Groups':(meta['records']+63)//64}]}
    negative=svm_rejection.prepare(WORK,shader,meta)
    (WORK/'cases.json').write_text(json.dumps([case,negative],indent=2)+'\n')
    print(result.stdout)

def verify(directory):
    def floats(p):
        a=array.array('f'); a.frombytes(p.read_bytes()); return a
    actual=floats(directory/'material.gpu.bin'); expected=floats(WORK/'material.expected.bin')
    inputs=floats(WORK/'material.input.bin')
    metadata=json.loads((WORK/'material-host.json').read_text())
    if len(actual)!=len(expected): raise RuntimeError('Mismatched output size')
    errors=[]; diffs=[]
    for i,(a,e) in enumerate(zip(actual,expected)):
        delta=abs(a-e); diffs.append(delta)
        tolerance=0 if i%24 in (7,15,20,21,22,23) else 5e-5+2e-4*abs(e)
        if not math.isfinite(a) or delta>tolerance: errors.append({'index':i,'record':i//24,'component':i%24,'actual':a,'expected':e})
    report={'passed':not errors,'records':len(actual)//24,'absoluteTolerance':5e-5,'relativeTolerance':2e-4,
        'mismatchedScalars':len(errors),'maximumAbsoluteError':max(diffs),'firstErrors':errors[:30],
        'errorsByMaterial':{str(m):sum(e['record']//metadata['recordsPerMaterial']==m for e in errors) for m in range(7)},
        'decodeErrorRecords':sum(actual[i]!=0 for i in range(21,len(actual),24)),
        'closureCountMismatches':sum(actual[i]!=expected[i] for i in range(20,len(actual),24)),
        'closureTypeMismatches':sum(actual[i]!=expected[i] for i in range(7,len(actual),24)),
        'labelMismatches':sum(actual[i]!=expected[i] for i in range(15,len(actual),24))}
    def horizon(record): return any(inputs[record*20+j] in (0,1) for j in (12,13))
    report['horizonBoundaryRecords']=sum(horizon(i) for i in range(len(actual)//24))
    report['horizonBoundaryScalarMismatches']=sum(horizon(e['record']) for e in errors)
    report['interiorScalarMismatches']=sum(not horizon(e['record']) for e in errors)
    report['firstInteriorErrors']=[e for e in errors if not horizon(e['record'])][:10]
    report['decoderRejectionTestsPassed']=svm_rejection.verify(WORK,directory)
    report['passed']=report['passed'] and report['decoderRejectionTestsPassed']
    (directory/'material-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('firstErrors','firstInteriorErrors')},indent=2))
    return report['passed']

def main():
    p=argparse.ArgumentParser(); p.add_argument('action',choices=['prepare','linux','android','verify']); p.add_argument('--serial'); p.add_argument('--results',type=Path)
    args=p.parse_args()
    if args.action=='prepare': prepare(); return
    if args.action=='verify': raise SystemExit(0 if verify(args.results or WORK) else 1)
    (WORK/'webgpu-compatibility.json').write_text(json.dumps(audit(),indent=2)+'\n')
    if args.action=='linux': native_runner.linux(REPO/'research',WORK); directory=WORK
    else:
        if not args.serial: p.error('--serial is required')
        directory=native_runner.android(REPO/'research',WORK,args.serial)
    raise SystemExit(0 if verify(directory) else 1)
if __name__=='__main__': main()
