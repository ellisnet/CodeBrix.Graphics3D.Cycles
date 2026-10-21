#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E5: compare binary outputs (films, sample bins, readbacks) of two gate output trees by sha256.
Usage: compare_trees.py BEFORE_DIR AFTER_DIR [suffix ...] -> JSON summary on stdout."""
import hashlib, json, sys
from pathlib import Path
a, b = Path(sys.argv[1]), Path(sys.argv[2]); suffixes = tuple(sys.argv[3:]) or ('.bin', '.f32', '.film', '.pfm', '.exr')
def files(root): return {str(p.relative_to(root)): p for p in root.rglob('*') if p.is_file() and p.name.endswith(suffixes) and 'arena' not in p.name}
fa, fb = files(a), files(b); h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
common = sorted(set(fa) & set(fb)); diff = [k for k in common if h(fa[k]) != h(fb[k])]
print(json.dumps(dict(before=str(a), after=str(b), common=len(common), identical=len(common)-len(diff), differing=diff,
                      onlyBefore=sorted(set(fa)-set(fb))[:20], onlyAfter=sorted(set(fb)-set(fa))[:20]), indent=1))
