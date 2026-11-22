#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Review of a Metal reference recording (read only): every film of the working tree against the committed one
(practical and strict criteria, the committed film as the reference), and each manifest entry's CPU comparison and
facts before and after. reference_review.py > review.json"""
import json, subprocess, struct
from pathlib import Path
F = 'tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64'
def head(path):
    p = subprocess.run(['git', 'show', 'HEAD:' + path], capture_output=True)
    return p.stdout if p.returncode == 0 else None
old_m = json.loads(head(F + '/manifest.json'))['references']
new_m = json.loads(Path(F, 'manifest.json').read_text())['references']
out = {'committedEntries': len(old_m), 'recordedEntries': len(new_m), 'new': [], 'existing': []}
for name in sorted(new_m):
    n = new_m[name]; o = old_m.get(name)
    row = {'name': name, 'cpuComparisonNow': n['cpuComparison'], 'factsNow': {k: n.get(k) for k in ('libraryVersion', 'device', 'backend', 'cyclesMetalRt', 'os', 'components')}}
    if o is None:
        out['new'].append(row); continue
    a = Path(F, n['file']).read_bytes(); b = head(F + '/' + o['file'])
    row['bytesIdentical'] = a == b
    row['shaChanged'] = n['sha256'] != o['sha256']
    if len(a) != len(b):
        row['lengthMismatch'] = [len(a), len(b)]
    else:
        fa = struct.unpack('<%df' % (len(a) // 4), a); fb = struct.unpack('<%df' % (len(b) // 4), b)
        d = [abs(x - y) for x, y in zip(fa, fb)]
        row['versusCommitted'] = {'maximumAbsoluteError': max(d), 'practicalFailures': sum(e > 5e-5 + 2e-4 * abs(y) for e, y in zip(d, fb)),
                                  'strictDifferences': sum(e > 1e-7 + 1e-7 * abs(y) for e, y in zip(d, fb)),
                                  'nonfinite': sum(x != x or x in (float('inf'), float('-inf')) for x in fa)}
    row['cpuComparisonCommitted'] = o['cpuComparison']
    row['factsCommitted'] = {k: o.get(k) for k in ('libraryVersion', 'device', 'backend', 'cyclesMetalRt', 'os', 'components', 'recordedUtc')}
    row['cpuComparisonGrew'] = n['cpuComparison']['maximumAbsoluteError'] > o['cpuComparison']['maximumAbsoluteError'] or n['cpuComparison']['practicalFailures'] > o['cpuComparison']['practicalFailures']
    out['existing'].append(row)
out['missingFromRecording'] = sorted(set(old_m) - set(new_m))
print(json.dumps(out, indent=1))
