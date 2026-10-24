#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 8 (copied from merge wave 7): summarize intel_compile_envelope.py outputs (entries, worst, entries over the limit)."""
import json, sys
for p in sys.argv[1:]:
    d = json.load(open(p)); e = d['entries']
    def ip(x):
        for k in ('maxEndIp', 'instructions', 'backendInstructions'):
            if k in x: return x[k]
        return max((v for v in x.values() if isinstance(v, int)), default=0)
    rows = sorted(e, key=ip, reverse=True)
    print(p, 'passed', d['passed'], 'limit', d['limit'], 'entries', len(e))
    for x in rows[:3]: print('   ', {k: v for k, v in x.items() if not isinstance(v, (list, dict))})
    over = [x for x in e if ip(x) >= d['limit']]
    for x in over: print('  OVER', {k: v for k, v in x.items() if not isinstance(v, (list, dict))})
