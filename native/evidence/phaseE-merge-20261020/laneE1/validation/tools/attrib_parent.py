#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Measurement only: for an entry, the fully inlined bytes under each function (subtree
size x instances), i.e. what removing one call site of it would save."""
import re, sys
from collections import Counter
from functools import lru_cache
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from wgsl_stats import parse
fns, text = parse(Path(sys.argv[1]).read_text())
calls = {n: [c for c in re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', b[b.index('{'):]) if c in fns and c != n] for n, b in fns.items()}
@lru_cache(None)
def size(n): return len(fns[n]) + sum(size(c) for c in calls[n])
count = Counter()
def walk(n, k=1):
    count[n] += k
    for c in calls[n]: walk(c, k)
walk(sys.argv[2])
for n, k in sorted(count.items(), key=lambda x: -size(x[0])*x[1])[:int(sys.argv[3])]:
    print(f'{size(n)*k:9d} {k:4d}x subtree {size(n):8d} {n}')
