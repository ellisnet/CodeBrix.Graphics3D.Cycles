#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the wave-5 diff (main now vs pre-merge.json) as a git-style patch. Base contents come from the merge-4
baseline state: for files that existed pre-merge, from native/work/merge5/base/ copies (saved before any wave-5 edit);
added files diff against /dev/null; removed files are listed (none expected). usage: wave5_diff.py OUT.patch"""
import hashlib, json, subprocess, sys
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); M = R/'native/work/merge5'
pre = json.load(open(M/'pre-merge.json'))['sourceSha256']
cur = {n: hashlib.sha256((R/n).read_bytes()).hexdigest() for n in subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=R).decode().split('\0') if n and (R/n).is_file()}
changed = sorted(n for n in cur if pre.get(n) != cur[n]); removed = sorted(set(pre) - set(cur))
out = []
for n in changed:
    base = M/'base'/n
    if n in pre:
        assert base.exists() and hashlib.sha256(base.read_bytes()).hexdigest() == pre[n], ('no verified base copy', n)
        a = str(base)
    else: a = '/dev/null'
    d = subprocess.run(['git', 'diff', '--no-index', '--no-color', '--full-index', a, str(R/n)], capture_output=True, text=True).stdout
    d = d.replace('a'+a if a != '/dev/null' else 'a/dev/null', 'a/'+n if a != '/dev/null' else '/dev/null').replace('b'+str(R/n), 'b/'+n)
    out.append(d)
Path(sys.argv[1]).write_text(''.join(out))
print(json.dumps({'changed': len(changed), 'added': sum(1 for n in changed if n not in pre), 'removed': removed}))
