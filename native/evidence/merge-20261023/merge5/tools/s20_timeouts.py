#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 5 (part 2; adds the regression-set jobs): S20 rerun timeouts = max(600 s, round(3 x this merge's Samsung tablet wall for the job))."""
import json
from pathlib import Path
R = Path(__file__).resolve().parents[1]/'runs/samsung'
src = {'transparent_lighting': 'transparent-m5', 'transparent_shadow': 'transparent-m5', 'object-moving-images': 'pixel-motion-x64',
       'deforming-moving-images': None, 'nodes_world': 'materials-m5', 'principled_svm': 'materials-m5', 'principled_bounce': 'materials-m5',
       'principled_render': 'materials-m5', 'nodes_svm': 'materials-m5', 'nodes_render': 'materials-m5',
       'triangle_sample': 'triangle-m5', 'surface': 'triangle-m5', 'glass_render': 'regression-m5', 'lighting_mis': 'regression-m5'}
out = {}
for job, run in src.items():
    wall = json.loads((R/run/'results.json').read_text()).get(job, {}).get('wallSeconds') if run else None
    if wall is None: wall, run = 110.8, 'merge1 Samsung motion-mw1 (lane P cutoff table; deforming not run on the tablet this merge)'
    out[job] = dict(tabletWallSeconds=wall, tabletSource=run, timeoutSeconds=max(600, round(3*wall)))
print(json.dumps(out, indent=2))
