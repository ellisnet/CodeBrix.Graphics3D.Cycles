# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Negative WGSL decoder tests; no undefined bytecode is passed to upstream C++."""
import json
import struct

def prepare(work,shader,metadata):
    op=metadata['opcodes']; diffuse=metadata['diffuseId']
    def f(x): return struct.unpack('<I',struct.pack('<f',x))[0]
    weight=[op['OP_SET_WEIGHT'],f(.75),f(.2),f(.08)]
    closure=[op['OP_BSDF'],diffuse,255,f(.75),f(.2),f(.08),f(.25),255]
    color=closure.copy(); color[3]=0x7fc000fd
    scalar=closure.copy(); scalar[6]=0x7fc000ff
    normal=closure.copy(); normal[7]=253
    # name, program, expected error, expected allocated closure count.
    cases=[('end',[op['OP_END']],0,0),('unknown-node',[0xffffffff],12,0),
        ('unknown-closure',[op['OP_BSDF'],999,255],9,0),
        ('jump-outside',[op['OP_JUMP'],0xffffffff,0,0],1,0),
        ('jump-cycle',[],13,0),('bad-color-stack',weight+color,14,0),
        ('bad-scalar-stack',weight+scalar,15,0),('bad-normal-stack',weight+normal,10,0),
        ('unsupported-mixture',weight+closure+closure,11,1),
        ('bad-geometry-stack',[op['OP_GEOMETRY'],1|(253<<24),0],4,0),
        ('root-overflow',[op['OP_END']],1,0),
        ('truncated-closure',[op['OP_BSDF']],6,0)]
    code=[0]*(len(cases)*4); inputs=[]; expected=[]
    for i,(name,program,error,count) in enumerate(cases):
        start=len(code)
        code[i*4:i*4+4]=[op['OP_JUMP'],start,0,0]
        if name=='jump-cycle': program=[op['OP_JUMP'],i*4,0,0]
        # Only the final test is truncated; all others terminate explicitly.
        code+=program+([] if name=='truncated-closure' else [op['OP_END']])
        inputs += [0,0,1,float(1<<30) if name=='root-overflow' else float(i),0,0,1,0,
                   0,0,1,0,.5,.5,0,0,0,0,1,0]
        expected.append({'name':name,'error':error,'count':count})
    (work/'svm_rejection.wgsl').write_text(shader)
    (work/'svm_rejection.input.bin').write_bytes(struct.pack('<'+'f'*len(inputs),*inputs))
    (work/'svm_rejection.output.bin').write_bytes(bytes(len(cases)*24*4))
    (work/'svm_rejection.code.bin').write_bytes(struct.pack('<'+'I'*len(code),*code))
    (work/'svm-rejection-expected.json').write_text(json.dumps(expected,indent=2)+'\n')
    return {'Name':'svm_rejection','Shader':'svm_rejection.wgsl',
        'Buffers':['svm_rejection.input.bin','svm_rejection.output.bin','svm_rejection.code.bin'],
        'Output':1,'Dispatches':[{'Entry':'main','Groups':1}]}

def verify(work,directory):
    expected=json.loads((work/'svm-rejection-expected.json').read_text())
    raw=(directory/'svm_rejection.gpu.bin').read_bytes()
    if len(raw)!=len(expected)*24*4: raise RuntimeError('Invalid rejection result size')
    values=struct.unpack('<'+'f'*(len(raw)//4),raw)
    results=[]
    for i,item in enumerate(expected):
        count,error=values[i*24+20:i*24+22]
        results.append(dict(item,actualError=error,actualCount=count,
            passed=error==item['error'] and count==item['count']))
    report={'passed':all(r['passed'] for r in results),'cases':results}
    (directory/'svm-rejection-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    return report['passed']
