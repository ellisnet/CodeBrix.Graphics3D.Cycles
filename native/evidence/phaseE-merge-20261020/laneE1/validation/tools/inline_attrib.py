#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Measurement only: per-function share of an entry's fully inlined WGSL size
(instances x own bytes), using wgsl_stats.py's textual call graph."""
import re, sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from wgsl_stats import parse
fns, text = parse(Path(sys.argv[1]).read_text())
calls = {n: [c for c in re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', b[b.index('{'):]) if c in fns and c != n] for n, b in fns.items()}
count = Counter()
def walk(n, k=1, depth=0):
    if depth > 60: return
    count[n] += k
    for c in calls[n]: walk(c, k, depth+1)
for entry in sys.argv[2].split(','):
    count.clear(); walk(entry)
    total = sum(count[n]*len(fns[n]) for n in count)
    print(f'{entry}: inlined {total} bytes')
    for n, k in sorted(count.items(), key=lambda x: -x[1]*len(fns[x[0]]))[:int(sys.argv[3]) if len(sys.argv) > 3 else 25]:
        print(f'  {k*len(fns[n]):9d} {k:5d}x {len(fns[n]):6d} {n}')
