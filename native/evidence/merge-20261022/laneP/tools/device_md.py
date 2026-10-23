#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane P: compact triage table (Markdown) for native/DEVICE.md from checkpoint/triage-table.json."""
import json
from pathlib import Path
P = Path(__file__).resolve().parents[1]
rows = json.loads((P/'checkpoint/triage-table.json').read_text())
def short(r):
    t = r['pixelResult']
    if t.startswith('FAIL: '):
        t = t[6:]
        for k, v in [('WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack', 'SVM error 8'),
                     ('WebGPU scheduler kernel reported invalid data (mask 24): invalid/uninitialized SVM stack unsupported SVM parameter', 'SVM error 24 (8+16)'),
                     ('WebGPU scheduler kernel reported invalid data (mask 1): invalid state/data', 'invalid state/data (mask 1)'),
                     ('Native compiled graphs SVM error mask=8', 'SVM error 8 (probe)')]:
            if k in t: return 'FAIL: ' + v
        if 'device is lost' in t: return 'FAIL: device lost'
        return 'FAIL: ' + t[:60]
    return t
def fmt(x): return '-' if x is None else (('%.1f' % x) if isinstance(x, (int, float)) else str(x))
out = ['| Corpus | Job | Samsung wall (s) | Pixel wall (s) | Cutoff (s) | Pixel result | Verdict |', '| --- | --- | --- | --- | --- | --- | --- |']
for r in rows:
    sw = fmt(r['samsungWall']) + ('' if r['samsungPassed'] in (True, None) else ' (fails)')
    out.append('| %s | %s | %s | %s | %s | %s | %s |' % (r['corpus'], r['job'], sw, fmt(r['pixelWall']), fmt(r['cutoff']), short(r), r['verdict']))
(P/'drafts/device-table.md').write_text('\n'.join(out)+'\n'); print(len(rows))
