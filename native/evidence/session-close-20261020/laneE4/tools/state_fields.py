#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: per-path state field diff of two arena dumps (state table pointer at p(7)=STATE).
Usage: state_fields.py CAPTURE_DIR A.bin B.bin STATE_TABLE PATHS [PATH ...]  (PATHS = number of path states)"""
import array, re, struct, sys
from pathlib import Path
cap=Path(sys.argv[1]); A=array.array('I',open(sys.argv[2],'rb').read()); B=array.array('I',open(sys.argv[3],'rb').read())
table=int(sys.argv[4]); npaths=int(sys.argv[5]); want=[int(x) for x in sys.argv[6:]]
src=(cap/'surface.wgsl').read_text()
fields={m.group(1):int(m.group(2)) for m in re.finditer(r'const (S_[A-Za-z0-9_]+):u32=(\d+)u;',src)}
bases=sorted((A[(table+o)//4],n) for n,o in fields.items() if n not in('S_queue_counter','S_sort_partition_divisor','S_sort_key_counter','S_volume_stack'))
bases=[b for b in bases if b[0]>0]
def owner(addr):
    best=None
    for i,(b,n) in enumerate(bases):
        if b<=addr: best=i
    if best is None: return None
    b,n=bases[best]; nxt=bases[best+1][0] if best+1<len(bases) else b+npaths*16
    size=nxt-b; stride=max(4,size//npaths) if size>0 else 4
    if addr-b>=npaths*stride: return None
    return n,(addr-b)//stride,(addr-b)%stride,stride
diff=[i for i in range(min(len(A),len(B))) if A[i]!=B[i]]
rows={}
for i in diff:
    o=owner(i*4)
    if o and (not want or o[1] in want): rows.setdefault(o[1],[]).append((o[0],o[2],A[i],B[i]))
    elif not o: rows.setdefault('other',[]).append((hex(i*4),0,A[i],B[i]))
for k in sorted(rows,key=str):
    if k=='other' and want: continue
    print('path',k,'differing words',len(rows[k]))
    for n,off,a,b in rows[k][:60]:
        fa=struct.unpack('<f',struct.pack('<I',a))[0]; fb=struct.unpack('<f',struct.pack('<I',b))[0]
        print('   %-40s +%-3s A=%-12d B=%-12d (%.9g vs %.9g)'%(n,off,a,b,fa,fb))
