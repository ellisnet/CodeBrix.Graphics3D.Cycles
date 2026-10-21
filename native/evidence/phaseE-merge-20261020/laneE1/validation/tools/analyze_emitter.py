#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E1 copy of the Phase C2 tool: offline per-field analysis of an emitter/triangle sampler gate's .actual/.reference bins (same criteria as the gate)."""
import array, math, sys, json
d = sys.argv[1]
a = array.array('f'); a.frombytes(open(d+'/run.actual.bin','rb').read())
e = array.array('f'); e.frombytes(open(d+'/run.reference.bin','rb').read())
names = ['valid','P.x','P.y','P.z','D.x','D.y','D.z','t','pdf','eval_fac','prim','object']
out = dict(samples=len(e)//12, valid=0, validityDifferences=0, strict=0, practicalFailures=0, failingSamples=[], byField={}, worst=None, maxAbs=0.0)
mx = 0
for i in range(len(e)//12):
    at = i*12
    if a[at] != e[at]: out['validityDifferences'] += 1; continue
    if not e[at]: continue
    out['valid'] += 1
    for j in range(1, 12):
        x, y = a[at+j], e[at+j]; err = abs(x-y); tol = 5e-5+2e-4*abs(y)
        out['maxAbs'] = max(out['maxAbs'], err)
        if not math.isfinite(x) or err > tol:
            out['practicalFailures'] += 1; out['byField'].setdefault(names[j], 0); out['byField'][names[j]] += 1
            out['failingSamples'].append(dict(sample=i, field=names[j], gpu=x, cpu=y, ratio=err/tol, prim=e[at+10], object=e[at+11]))
        if err > 1e-7+1e-7*abs(y): out['strict'] += 1
        if err/tol > mx: mx = err/tol; out['worst'] = dict(sample=i, field=names[j], gpu=x, cpu=y, ratio=err/tol)
print(json.dumps(out, indent=1))
