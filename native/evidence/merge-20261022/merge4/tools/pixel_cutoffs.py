#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4: Pixel 4 XL cutoffs for the provisional materials jobs: max(5 s, round(3 x Samsung wall)).
Samsung wall = this merge's runs/samsung/materials-m4 job wallSeconds (time to pass or to failure); lane S's
device-times.tsv when this merge's run has none."""
import json
from pathlib import Path
M = Path(__file__).resolve().parents[1]
LANE_S = {'principled_svm': (126, 'lane S materials-prin1'), 'principled_bounce': (320, 'lane S materials-prin1'),
          'principled_render': (54, 'lane S materials-s2'), 'nodes_svm': (1014, 'lane S materials-fade (512 cases, then driver SIGSEGV)'),
          'nodes_render': (109, 'lane S materials-fade')}
res = json.loads((M/'runs/samsung/materials-m4/results.json').read_text())
out = {}
for job, (fallback, source) in LANE_S.items():
    r = res.get(job, {}); wall = r.get('wallSeconds')
    if wall is not None and not r.get('timeout'):
        out[job] = dict(samsungWallSeconds=wall, samsungPassed=bool(r.get('passed')), source='merge4 runs/samsung/materials-m4/results.json')
    else:
        out[job] = dict(samsungWallSeconds=fallback, samsungPassed=False, source=source)
    out[job]['cutoffSeconds'] = max(5, round(3*out[job]['samsungWallSeconds']))
print(json.dumps(out, indent=2))
