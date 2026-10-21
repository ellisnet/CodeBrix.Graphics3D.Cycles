#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Measurement only: match shadow paths of two replay arenas by (pixel, sample) and
report those whose queued kernel or ray differs (Phase E1 item 2 diagnosis)."""
import struct, sys
A = open(sys.argv[1], 'rb').read(); B = open(sys.argv[2], 'rb').read(); table = 2600608
def word(buf, at): return struct.unpack_from('<I', buf, at)[0]
def flt(buf, at, n=1): return struct.unpack_from('<%df' % n, buf, at)
def base(buf, off): return word(buf, table+off)
def paths(buf):
    out = {}
    for i in range(4096):
        qk = struct.unpack_from('<H', buf, 1873360+2*i)[0]
        pix = word(buf, base(buf, 976)+4*i); smp = word(buf, base(buf, 984)+4*i); bounce = word(buf, base(buf, 1008)+4*i)
        if pix > 10000: continue
        P = flt(buf, base(buf, 1168)+12*i, 3); D = flt(buf, base(buf, 1176)+12*i, 3); tmax = flt(buf, base(buf, 1192)+4*i)[0]
        hit = (flt(buf, base(buf, 1240)+4*i)[0], word(buf, base(buf, 1264)+4*i), word(buf, base(buf, 1272)+4*i))
        lo = (word(buf, base(buf, 1224)+4*i), word(buf, base(buf, 1232)+4*i))
        out.setdefault((pix, smp, bounce), []).append(dict(index=i, qk=qk, P=P, D=D, tmax=tmax, hit=hit, light=lo))
    return out
a = paths(A); b = paths(B)
for key in sorted(set(a) | set(b)):
    ka = sorted((x['qk'], x['tmax']) for x in a.get(key, [])); kb = sorted((x['qk'], x['tmax']) for x in b.get(key, []))
    if [k for k, _ in ka] != [k for k, _ in kb]:
        print('pixel,sample,bounce', key); print('  A', a.get(key)); print('  B', b.get(key))
