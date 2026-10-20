#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase C3: compare per-gate numbers with Phase C1's summary (read-only input)."""
import json, sys
c1 = json.load(open(sys.argv[1])); c3 = json.load(open(sys.argv[2]))
def flat(d, prefix=''):
    out = {}
    if isinstance(d, dict):
        for k, v in d.items():
            if k in ('run', 'seconds'): continue
            out.update(flat(v, prefix + '/' + k))
    elif isinstance(d, (int, float, bool)) or d is None:
        out[prefix] = d
    return out
rows = []
for name in c3:
    if name not in c1: continue
    a, b = flat(c1[name]), flat(c3[name])
    for k in sorted(set(a) & set(b)):
        if a[k] != b[k]: rows.append((name + k, a[k], b[k]))
    same = sum(1 for k in set(a) & set(b) if a[k] == b[k])
    print(f'{name}: {same} equal values, {sum(1 for r in rows if r[0].startswith(name + "/"))} differ, only-C1 {len(set(a)-set(b))}, only-C3 {len(set(b)-set(a))}')
for r in rows: print('DIFF', *r)
