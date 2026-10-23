#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Manifest/compare main's tracked+untracked non-ignored files.
usage: manifest.py check <baseline.json> | write <out.json> <scope text>"""
import datetime, hashlib, json, subprocess, sys
from pathlib import Path
root = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
def current():
    files = [n for n in subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=root).decode().split('\0') if n and (root / n).is_file()]
    return {n: hashlib.sha256((root / n).read_bytes()).hexdigest() for n in files}
cur = current()
if sys.argv[1] == 'check':
    base = json.load(open(sys.argv[2]))['sourceSha256']
    missing = sorted(set(base) - set(cur)); extra = sorted(set(cur) - set(base))
    diff = sorted(n for n in set(base) & set(cur) if base[n] != cur[n])
    print('baseline', len(base), 'current', len(cur), 'missing', len(missing), 'extra', len(extra), 'changed', len(diff))
    for t, l in (('MISSING', missing), ('EXTRA', extra), ('CHANGED', diff)):
        for n in l[:200]: print(t, n)
    sys.exit(0 if not (missing or extra or diff) else 1)
else:
    out = Path(sys.argv[2]); out.parent.mkdir(parents=True, exist_ok=True)
    m = {'createdUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'sourceRepo': str(root),
         'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root).decode().strip(),
         'files': len(cur), 'sourceSha256': cur, 'scope': sys.argv[3]}
    out.write_text(json.dumps(m, indent=2) + '\n'); print('wrote', out, len(cur))
