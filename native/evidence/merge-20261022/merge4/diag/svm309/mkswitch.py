#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4 diagnostic: replace function-scope Horner coefficient arrays (var X=array<vec2<f32>,N>(...); X[step])
with a module-scope switch lookup fn X_at(i) (no private array). Usage: mkswitch.py IN OUT"""
import re, sys
t = open(sys.argv[1]).read(); extra = []
for m in list(re.finditer(r'var (\w+_terms)=array<vec2<f32>,(\d+)>\((.*?)\);', t, re.S)):
    name, n, body = m.group(1), int(m.group(2)), m.group(3)
    items = re.findall(r'vec2<f32>\([^)]*\)', body); assert len(items) == n, name
    cases = ''.join('case %du:{return %s;}' % (i, v) for i, v in enumerate(items[:-1]))
    extra.append('fn %s_at(i:u32)->vec2<f32>{switch(i){%sdefault:{return %s;}}}\n' % (name, cases, items[-1]))
    t = t.replace(m.group(0), '').replace(name+'[step]', name+'_at(step)')
assert '_terms[' not in t
i = t.index('fn precise_disk_sincos'); t = t[:i]+''.join(extra)+t[i:]
open(sys.argv[2], 'w').write(t); print(len(extra), 'arrays replaced')
