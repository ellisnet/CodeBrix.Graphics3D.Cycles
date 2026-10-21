#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E2: arena diff ignoring few-ulp float differences (same sign, |int delta|<=TOL)."""
import sys, array
a=array.array('I',open(sys.argv[1],'rb').read()); b=array.array('I',open(sys.argv[2],'rb').read()); tol=int(sys.argv[4]) if len(sys.argv)>4 else 4096
d=[i for i in range(min(len(a),len(b))) if a[i]!=b[i] and not ((a[i]>>31)==(b[i]>>31) and abs(a[i]-b[i])<=tol and (a[i]&0x7f800000)!=0)]
print('big words differ',len(d))
r=[]
for i in d:
    if r and i-r[-1][1]<=16: r[-1][1]=i; r[-1][2]+=1
    else: r.append([i,i,1])
for s,e,n in r[:int(sys.argv[3]) if len(sys.argv)>3 else 40]:
    print('bytes %d..%d n=%d  first: A=%d(0x%x) B=%d(0x%x)'%(s*4,e*4,n,a[s],a[s],b[s],b[s]))
