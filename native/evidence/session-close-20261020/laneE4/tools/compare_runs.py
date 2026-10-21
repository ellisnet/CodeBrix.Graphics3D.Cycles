#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: before/after comparison of two validation output trees: every <prefix>.gpu.film.bin with a CPU film beside
it; GPU film identity, own gpu/repeat identity, strict/practical/max against the CPU film. Usage: compare_runs.py BEFORE AFTER"""
import sys, numpy as np
from pathlib import Path
A=Path(sys.argv[1]); B=Path(sys.argv[2])
def st(g,r):
    g=g.astype(np.float64); r=r.astype(np.float64); e=abs(g-r)
    return int((e>1e-7+1e-7*abs(r)).sum()), int((e>5e-5+2e-4*abs(r)).sum()), float(e.max()) if e.size else 0.0, int(np.isnan(g).sum())
rows=[]; tot=dict(same=0,changed=0,better=0,worse=0,equal=0)
for gb in sorted(B.rglob('*.gpu.film.bin')):
    rel=gb.relative_to(B); ga=A/rel
    cb=gb.with_name(gb.name.replace('.gpu.film.bin','.cpu.film.bin')); ca=ga.with_name(cb.name)
    if not (ga.exists() and cb.exists() and ca.exists()): continue
    fa=np.fromfile(ga,'<f4'); fb=np.fromfile(gb,'<f4'); ra=np.fromfile(ca,'<f4'); rb=np.fromfile(cb,'<f4')
    if fa.shape!=fb.shape or ra.shape!=fa.shape or rb.shape!=fb.shape: continue
    rep=gb.with_name(gb.name.replace('.gpu.','.repeat.')); own=None
    if rep.exists(): own=int((np.fromfile(rep,'<f4').view('u4')!=fb.view('u4')).sum())
    sa=st(fa,ra); sb=st(fb,rb); same=(fa.view('u4')==fb.view('u4')).all(); cpu_same=(ra.view('u4')==rb.view('u4')).all()
    tot['same' if same else 'changed']+=1
    if not same: tot['better' if (sb[0]<sa[0] or (sb[0]==sa[0] and sb[2]<sa[2])) else 'equal' if sb[0]==sa[0] and sb[2]==sa[2] else 'worse']+=1
    rows.append((str(rel), 'same' if same else 'CHANGED', 'cpu-same' if cpu_same else 'CPU-DIFF', sa, sb, own))
for r in rows: print('%-90s %-7s %-8s strict %d->%d practical %d->%d max %.3g->%.3g nan %d->%d own-repeat-diff %s'%(r[0][:90],r[1],r[2],r[3][0],r[4][0],r[3][1],r[4][1],r[3][2],r[4][2],r[3][3],r[4][3],r[5]))
print('TOTAL', len(rows), tot)
