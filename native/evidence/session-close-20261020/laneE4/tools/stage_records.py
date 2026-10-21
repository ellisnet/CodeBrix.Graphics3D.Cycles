#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: compare surface stage records (base p(24), 360 bytes each; weight +304, light sample +308..+360).
Usage: stage_records.py A.bin B.bin BASE COUNT QUEUE [ULP]"""
import array,struct,sys
A=array.array('I',open(sys.argv[1],'rb').read());B=array.array('I',open(sys.argv[2],'rb').read())
base=int(sys.argv[3]);count=int(sys.argv[4]);queue=int(sys.argv[5]);ulp=int(sys.argv[6]) if len(sys.argv)>6 else 0
f=lambda x:struct.unpack('<f',struct.pack('<I',x))[0]
names={304:'weight',308:'ls.P.x',312:'ls.P.y',316:'ls.P.z',320:'ls.D.x',324:'ls.D.y',328:'ls.D.z',332:'ls.t',336:'ls.pdf',340:'ls.eval_fac',344:'ls.shader',348:'ls.object',352:'ls.prim',356:'ls.is_light'}
for i in range(count):
    at=base+i*360;rows=[]
    for o in range(0,360,4):
        a=A[(at+o)//4];b=B[(at+o)//4]
        if a!=b and abs(a-b)>ulp: rows.append((o,a,b))
    if rows:
        print('id',i,'path',A[(queue+4*i)//4],'state A/B',A[at//4],B[at//4],'fields',len(rows))
        for o,a,b in rows[:24]: print('   +%-3d %-12s A=%-11d B=%-11d %.9g vs %.9g'%(o,names.get(o,''),a,b,f(a),f(b)))
