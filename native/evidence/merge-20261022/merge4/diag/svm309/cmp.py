#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4 diagnostic: compare an arena replay output with the imported glibc reference records (practical criterion)."""
import json, struct, sys, array
cap = json.load(open('run.capture.json')); o = cap['outputOffset']; rs = cap['recordSize']; n = cap['count']
out = open(sys.argv[1], 'rb').read(); ref = open('run.reference.bin', 'rb').read()
fl = [f for f in cap['fields'] if f['floating']]
bad = []; worst = 0
for i in range(n):
    a = out[o+i*rs:o+(i+1)*rs]; b = ref[i*rs:(i+1)*rs]
    for f in fl:
        for k in range(f['size']//4):
            av = struct.unpack_from('<f', a, f['offset']+4*k)[0]; ev = struct.unpack_from('<f', b, f['offset']+4*k)[0]
            e = abs(av-ev)
            if not (e <= 5e-5+2e-4*abs(ev)) and not (av != av and ev != ev): bad.append((i, f['name'], k, av, ev))
print(sys.argv[1], 'practical failures', len(bad), 'paths', sorted({b[0] for b in bad})[:20]); [print(' ', b) for b in bad[:6]]
