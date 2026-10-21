#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E5: judge a sampler_probe dump against the device-scene glibc reference with the gate's own
criteria (practical 5e-5+2e-4*|ref|, strict 1e-7+1e-7*|ref| reported, NaN/Inf fail, guards).
Usage: judge.py PULLED_DIR REFERENCE_SAMPLES_F32 [N]"""
import sys, json, numpy as np
d, refp = sys.argv[1], sys.argv[2]
out = np.fromfile(d+'/run.e5-output-raw.bin', '<f4'); ref = np.fromfile(refp, '<f4'); n = len(ref)//12
if len(sys.argv) > 3: n = int(sys.argv[3])
g = out[8:8+len(ref)].reshape(-1, 12)[:n]; r = ref.reshape(-1, 12)[:n]
guards = bool(np.all(out[:8] == -1234) and np.all(out[-8:] == -1234))
validity = bool(np.array_equal(g[:, 0], r[:, 0])); v = r[:, 0] != 0
gv, rv = g[v, 1:].astype(np.float64), r[v, 1:].astype(np.float64)
finite = bool(np.isfinite(gv).all() and np.isfinite(rv).all()); err = np.abs(gv-rv)
practical = int((err > 5e-5+2e-4*np.abs(rv)).sum()); strict = int((err > 1e-7+1e-7*np.abs(rv)).sum())
res = dict(samples=n, valid=int(v.sum()), guardsIntact=guards, validityEqual=validity, finite=finite, practical=practical,
           strict=strict, maximumAbsoluteError=float(err.max()), passed=guards and validity and finite and practical == 0 and v.sum() > 100)
print(json.dumps(res))
