#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4: S20 rerun timeouts = max(600 s, round(3 x this merge's Samsung tablet wall for the job))."""
import json
from pathlib import Path
R = Path(__file__).resolve().parents[1]/'runs/samsung'
src = {'transparent_lighting': 'transparent-m4', 'transparent_shadow': 'transparent-m4', 'object-moving-images': 'pixel-motion-x64',
       'deforming-moving-images': None, 'nodes_world': 'materials-m4', 'principled_svm': 'materials-m4', 'principled_bounce': 'materials-m4',
       'principled_render': 'materials-m4', 'nodes_svm': 'materials-m4', 'nodes_render': 'materials-m4'}
out = {}
for job, run in src.items():
    wall = json.loads((R/run/'results.json').read_text()).get(job, {}).get('wallSeconds') if run else None
    if wall is None: wall, run = 110.8, 'merge1 Samsung motion-mw1 (lane P cutoff table; deforming not run on the tablet this merge)'
    out[job] = dict(tabletWallSeconds=wall, tabletSource=run, timeoutSeconds=max(600, round(3*wall)))
print(json.dumps(out, indent=2))
