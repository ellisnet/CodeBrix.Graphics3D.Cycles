#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Warning sites (file, warning flag) of codebrix sources in build logs, wave 7 vs wave 8 (line numbers ignored).
usage: warn_sites.py OLD_LOG[,OLD_LOG] NEW_LOG[,NEW_LOG]"""
import re, sys
def sites(logs):
    s = {}
    for log in logs.split(','):
        for m in re.finditer(r'(/[^\s:]*CodeBrix\.Graphics3D\.Cycles/native/[^\s:]+):(\d+):\d+: warning: (.*?)(\[-W[^\]]+\])?$', open(log, errors='replace').read(), re.M):
            k = (m.group(1).split('CodeBrix.Graphics3D.Cycles/')[1], m.group(4) or m.group(3)[:60]); s.setdefault(k, set()).add(int(m.group(2)))
    return s
old, new = sites(sys.argv[1]), sites(sys.argv[2])
print(f'old sites {len(old)}, new sites {len(new)}')
for k in sorted(set(new) - set(old)): print('NEW', k, sorted(new[k])[:5])
for k in sorted(set(old) - set(new)): print('GONE', k)
