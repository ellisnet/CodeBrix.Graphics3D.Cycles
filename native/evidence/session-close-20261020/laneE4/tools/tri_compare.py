#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: before/after table of two triangle_lighting_gate outputs (per-case GPU vs CPU film, films identity)."""
import json, sys
import numpy as np
from pathlib import Path
A=Path(sys.argv[1]); B=Path(sys.argv[2])
def case(root,c):
    d=root/'triangle_lighting/run'/('case-%d'%c)
    g=np.fromfile(d/'run.gpu.film.bin','<f4'); r=np.fromfile(d/'run.cpu.film.bin','<f4')
    e=abs(g.astype(np.float64)-r); strict=int((e>1e-7+1e-7*abs(r)).sum()); prac=int((e>5e-5+2e-4*abs(r)).sum())
    return g,r,strict,prac,float(e.max()),int((g.view('u4')!=r.view('u4')).sum())
for c in range(6):
    ga,ra,sa,pa,ma,ba=case(A,c); gb,rb,sb,pb,mb,bb=case(B,c)
    print('case-%d cpu film %s | gpu film %s | strict %d->%d practical %d->%d max %.3g->%.3g bitwise-differing-from-cpu %d->%d'%(c,'same' if (ra.view('u4')==rb.view('u4')).all() else 'DIFF','same' if (ga.view('u4')==gb.view('u4')).all() else 'changed',sa,sb,pa,pb,ma,mb,ba,bb))
for root in (A,B):
    r=json.loads((root/'results.json').read_text())
    tl=r['triangle_lighting']; ts=r['triangle_sample']
    print(root.parent.name,'triangle_lighting strict',tl['strictDifferences'],'max',tl['maximumAbsoluteError'],'| sampler',{k:ts.get(k) for k in ('strictDifferences','maximumAbsoluteError','passed')},'| replays',[ (r['replay-%d'%i].get('results',{}) or {}).get('exactSchedulingReadbacks', r['replay-%d'%i].get('exactSchedulingReadbacks')) for i in range(6)])
