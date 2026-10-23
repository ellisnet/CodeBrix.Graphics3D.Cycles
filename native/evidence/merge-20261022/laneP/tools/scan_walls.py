#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane P: collect per-job wall times from device run records (results.json + status.json) for a device.
Usage: scan_walls.py DEVICE OUT.json ROOT..."""
import json, sys, os
from pathlib import Path
dev, out, roots = sys.argv[1], Path(sys.argv[2]), sys.argv[3:]
rows = []
for root in roots:
    for res in Path(root).rglob('results.json'):
        parts = res.parts
        if dev not in str(res.parent): continue
        try: r = json.loads(res.read_text())
        except Exception: continue
        if not isinstance(r, dict): continue
        st = res.parent/'status.json'
        try: s = json.loads(st.read_text())
        except Exception: s = {}
        for label, e in r.items():
            if not isinstance(e, dict): continue
            wall = e.get('wallSeconds') or (e.get('report') or {}).get('wallSeconds') if isinstance(e.get('report'), dict) else e.get('wallSeconds')
            rows.append(dict(source=str(res.parent), mtime=os.path.getmtime(res), corpus=s.get('corpus'), runLabel=s.get('label'), job=label,
                             wall=wall, passed=e.get('passed'), timeout=e.get('timeout'), notRun=e.get('notRun'), corpusWall=s.get('wallSeconds')))
out.write_text(json.dumps(rows, indent=1))
print(len(rows), 'rows')
