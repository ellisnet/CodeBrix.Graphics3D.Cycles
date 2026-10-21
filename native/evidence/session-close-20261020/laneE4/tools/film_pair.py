#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: GPU-vs-CPU film statistics of two run prefixes (before/after): strict, practical, max, bitwise."""
import sys, numpy as np
def stats(prefix):
    g=np.fromfile(prefix+'.gpu.film.bin','<f4').astype(np.float64); r=np.fromfile(prefix+'.cpu.film.bin','<f4').astype(np.float64)
    e=abs(g-r); return dict(strict=int((e>1e-7+1e-7*abs(r)).sum()),practical=int((e>5e-5+2e-4*abs(r)).sum()),max=float(e.max()),bitwise=int((g!=r).sum()),nan=int(np.isnan(g).sum()))
a=stats(sys.argv[1]); b=stats(sys.argv[2])
same_cpu=open(sys.argv[1]+'.cpu.film.bin','rb').read()==open(sys.argv[2]+'.cpu.film.bin','rb').read()
print('cpu film identical' if same_cpu else 'CPU FILM DIFFERS', '| before',a,'| after',b)
