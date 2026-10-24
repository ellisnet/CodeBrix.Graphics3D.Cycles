#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 6 (part 2): S20 per-job timeouts = max(600 s, round(3 x Samsung tablet wall for the job)). The Samsung wall is taken from this
merge's tablet run when it exists, else from merge 5's tablet run (the S20 chain starts before the tablet chain has produced its runs)."""
import json
from pathlib import Path
W = Path(__file__).resolve().parents[2]
src = {'transparent_lighting': 'transparent-m', 'transparent_shadow': 'transparent-m', 'object-moving-images': 'pixel-motion-x64',
       'deforming-moving-images': None, 'principled_svm': 'materials-m', 'principled_bounce': 'materials-m',
       'principled_render': 'materials-m', 'triangle_sample': 'triangle-m', 'surface': 'triangle-m', 'glass_render': 'regression-m', 'lighting_mis': 'regression-m'}
def wall(job, run):
    for merge, suffix in (('merge6', '6'), ('merge5', '5')):
        name = run if run.endswith('x64') else run + suffix
        p = W/merge/'runs/samsung'/name/'results.json'
        if p.exists():
            w = json.loads(p.read_text()).get(job, {}).get('wallSeconds')
            if w is not None: return w, merge+'/'+name
    return None, None
out = {}
for job, run in src.items():
    w, s = wall(job, run) if run else (None, None)
    if w is None: w, s = 110.8, 'merge1 Samsung motion-mw1 (lane P cutoff table; deforming not run on the tablet)'
    out[job] = dict(tabletWallSeconds=w, tabletSource=s, timeoutSeconds=max(600, round(3*w)))
print(json.dumps(out, indent=2))
