#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 7: compare this tree's CPU oracle films (film-0) with lane m1-oracle's final hash table and the old (pre-
baseline) hashes. usage: check_oracle_hashes.py TABLE.json ORACLE_ROOT... (each ORACLE_ROOT/oracle/<scene>/cpu-x64-linux)"""
import hashlib, json, sys
from pathlib import Path
t = json.load(open(sys.argv[1]))['films']
old = json.load(open(sys.argv[1].replace('new-final', 'old-final')))['films']
oldh = {r['scene']: r['oldCpu'] for r in old}
roots = [Path(p) for p in sys.argv[2:]]
res = dict(match=[], matchOld=[], neither=[], missing=[])
for r in t:
    sc = r['scene']; f = None
    for root in roots:
        for cand in (root/'oracle'/sc/'cpu-x64-linux'/'film-0.f32',):
            if cand.exists(): f = cand
    if f is None: res['missing'].append(sc); continue
    h = hashlib.sha256(f.read_bytes()).hexdigest()
    (res['match'] if h == r['newCpu'] else res['matchOld'] if h == oldh.get(sc) else res['neither']).append((sc, h[:8], r['newCpu'][:8]))
for k, v in res.items(): print(k, len(v)); [print('   ', x) for x in v if k != 'match']
