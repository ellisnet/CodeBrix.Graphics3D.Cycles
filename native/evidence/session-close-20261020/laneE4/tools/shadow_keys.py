#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: compare the live shadow-path entries (pixel, sample, bounce) of two arena dumps.
Usage: shadow_keys.py CAPTURE A.bin B.bin STATE_TABLE"""
import array,re,sys
src=open(sys.argv[1]+'/surface.wgsl').read()
f={m.group(1):int(m.group(2)) for m in re.finditer(r'const (S_[A-Za-z0-9_]+):u32=(\d+)u;',src)}
t=int(sys.argv[4])
def load(p): return array.array('I',open(p,'rb').read())
def col(A,name,n=4096): b=A[(t+f[name])//4]; return A[b//4:b//4+n]
def keys(A):
    for addr in range(1775056,1775056+4096*4,4): pass
    pix=col(A,'S_shadow_path_render_pixel_index'); smp=col(A,'S_shadow_path_sample'); bnc=col(A,'S_shadow_path_bounce'); qk=col(A,'S_shadow_path_queued_kernel')
    return pix,smp,bnc,qk
L=load(sys.argv[2]); S=load(sys.argv[3])
for nm,A in (('A',L),('B',S)):
    pix,smp,bnc,qk=keys(A); live=[i for i in range(4096) if qk[i]!=0]
    print(nm,'queued_kernel values',sorted(set(qk))[:8],'live',len(live),'max slot',max(live) if live else None)
