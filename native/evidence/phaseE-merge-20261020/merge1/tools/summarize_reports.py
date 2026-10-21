#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 1 copy of X2's summarize.py. Flatten gate reports under a run directory into name -> {strict/max/precision fields} JSON."""
import json, sys
from pathlib import Path
KEYS = ('strict', 'maximum', 'max', 'precision', 'passed', 'readbacks', 'cases', 'paths', 'blocked', 'unoccluded', 'repeat', 'film')
def pick(d):
    return {k: v for k, v in d.items() if isinstance(v, (int, float, bool, str)) and any(t in k.lower() for t in KEYS) and len(str(v)) < 80}
out = {}
root = Path(sys.argv[1])
for f in sorted(root.rglob('*')):
    if f.name not in ('stdout.txt', 'slot.stdout.txt', 'results.json', 'validation.json', 'result.json') or f.stat().st_size > 5_000_000: continue
    try: data = json.loads(f.read_text().strip().splitlines()[-1] if f.suffix == '.txt' else f.read_text())
    except Exception: continue
    rel = str(f.relative_to(root))
    if f.name == 'results.json' and isinstance(data, dict) and all(isinstance(v, dict) for v in data.values()):
        for k, v in data.items():
            if isinstance(v, dict) and 'report' in v and isinstance(v['report'], dict): v = v['report']
            out[rel + ':' + k] = pick(v)
    elif isinstance(data, dict): out[rel] = pick(data)
json.dump(out, open(sys.argv[2], 'w'), indent=1, sort_keys=True); print(len(out), 'reports')
