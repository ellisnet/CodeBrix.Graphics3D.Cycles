#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Describe observed Linux discrepancies without changing acceptance tolerances."""
import array
import json
import math
import struct
from pathlib import Path

WORK = Path(__file__).resolve().parent/'work'
def data(name):
    a = array.array('f'); a.frombytes((WORK/name).read_bytes()); return a
def sub(a,b): return [x-y for x,y in zip(a,b)]
def add(a,b): return [x+y for x,y in zip(a,b)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def describe(p,d,a,b,c):
    v0,v1,v2 = sub(a,p),sub(b,p),sub(c,p)
    e0,e1,e2 = sub(v2,v0),sub(v0,v1),sub(v1,v2)
    u,v,w = dot(cross(e0,add(v2,v0)),d),dot(cross(e1,add(v0,v1)),d),dot(cross(e2,add(v1,v2)),d)
    n = cross(e1,e0)
    return {'doublePrecisionBarycentrics': [u/(u+v+w),v/(u+v+w)],
            'absoluteCosineRayToNormal': abs(dot(n,d))/math.sqrt(dot(n,n)*dot(d,d)),
            'doublePrecisionIsDiagnosticOnly': True}

def analyze():
    report = json.loads((WORK/'validation.json').read_text())
    findings = []
    for test in report['tests']:
        name = test['name']; inputs = data(name+'.input.bin')
        for error in test['firstErrors']:
            record = error['index']//4
            item = dict(case=name,record=record,**error)
            if name == 'material_math': item['input'] = list(inputs[record*4:record*4+4])
            if name == 'triangles':
                row = inputs[record*20:record*20+20]
                item.update(describe(row[:3],row[4:7],row[8:11],row[12:15],row[16:19]))
            if name == 'bvh':
                scene = (WORK/'bvh.scene.bin').read_bytes()
                vertex_base = int(struct.unpack_from('<4f',scene)[3])
                primitive = int(data('bvh.expected.bin')[record*4+3])
                verts = [struct.unpack_from('<3f',scene,(vertex_base+primitive*3+i)*16) for i in range(3)]
                row = inputs[record*8:record*8+8]
                item.update(describe(row[:3],row[4:7],*verts))
            findings.append(item)
    (WORK/'numerical-analysis.json').write_text(json.dumps(findings,indent=2)+'\n')
    print(json.dumps(findings,indent=2))

if __name__ == '__main__': analyze()
