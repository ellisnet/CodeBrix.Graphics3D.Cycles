#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E2: word-level diff of two arena dumps, grouped into contiguous ranges."""
import sys, array
a=array.array('I',open(sys.argv[1],'rb').read()); b=array.array('I',open(sys.argv[2],'rb').read())
d=[i for i in range(min(len(a),len(b))) if a[i]!=b[i]]
print('words differ',len(d))
r=[]
for i in d:
    if r and i-r[-1][1]<=16: r[-1][1]=i; r[-1][2]+=1
    else: r.append([i,i,1])
for s,e,n in r[:int(sys.argv[3]) if len(sys.argv)>3 else 40]:
    print('bytes %d..%d n=%d  first: A=%d B=%d'%(s*4,e*4,n,a[s],b[s]))
