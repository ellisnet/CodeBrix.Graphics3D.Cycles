#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compare two emitter-sampler .bin files field by field (bitwise counts and largest relative diffs)."""
import array, sys
names = ['valid','P.x','P.y','P.z','D.x','D.y','D.z','t','pdf','eval_fac','prim','object']
a = array.array('f'); a.frombytes(open(sys.argv[1],'rb').read())
b = array.array('f'); b.frombytes(open(sys.argv[2],'rb').read())
diff = {n: 0 for n in names}; samples = set(); worst = []
for i in range(len(a)//12):
    for j in range(12):
        x, y = a[i*12+j], b[i*12+j]
        if x != y:
            diff[names[j]] += 1; samples.add(i)
            worst.append((abs(x-y)/(5e-5+2e-4*abs(y)), i, names[j], x, y))
worst.sort(reverse=True)
print('differing floats', sum(diff.values()), 'samples', len(samples), {k: v for k, v in diff.items() if v})
for w in worst[:int(sys.argv[3]) if len(sys.argv) > 3 else 8]: print('  ratio %.3g sample %d %s %r %r' % w)
