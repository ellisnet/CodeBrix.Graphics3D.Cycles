#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Print one emitter/triangle sampler record from several .bin files, with float bits."""
import array, struct, sys
i = int(sys.argv[1])
names = ['valid','P.x','P.y','P.z','D.x','D.y','D.z','t','pdf','eval_fac','prim','object']
for path in sys.argv[2:]:
    a = array.array('f'); a.frombytes(open(path,'rb').read())
    r = a[i*12:(i+1)*12]
    print(path.split('/work/')[-1])
    print('  ' + '  '.join(f'{n}={v:.9g}({struct.unpack("<I",struct.pack("<f",v))[0]:08x})' for n, v in zip(names, r)))
