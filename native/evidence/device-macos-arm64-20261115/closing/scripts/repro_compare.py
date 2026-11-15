#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Information check (reproducibility): the closing build's hashes against earlier builds.
repro_compare.py CLOSING.json > repro-compare.md
- the C27 scratch tables (native/work/repro-20261114/tables/F-a1.json, F-bb.json: 'build/...' keys; the code changed since);
- the batch-final build (native/work/partD-20261114/final/hashes.txt: host library and runner only);
- the build tree deleted at the start of phase 1 (pre-delete-build-hashes.json: 49cd3a4a plus the final batch, uncommitted)."""
import json
import sys
from pathlib import Path

REPO = Path('/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles')
R = REPO/'native/work/macos-arm64-closing-20261115'
closing = json.loads(Path(sys.argv[1]).read_text())['files']


def group(key):
    if key == 'build/host/lib/libcodebrix_cycles_host.dylib': return 'host library'
    if key.startswith('build/host/bin/'): return 'lifecycle/reference executables'
    if key.startswith('build/host/lib/') and key.endswith('.a'): return 'Cycles static libraries'
    if key.startswith('build/kernel/'): return 'staged kernel closure'
    if key.startswith('build/prefix/') and key.endswith('.dylib'): return 'dependency dylibs'
    if key.startswith('build/prefix/') and key.endswith('.a'): return 'dependency static libraries'
    if key == 'build/prefix-marker': return 'dependency prefix record'
    if key == 'build/runner': return 'oracle runner'
    return 'other'


def table(title, other):
    rows = []
    groups = {}
    for key in sorted(set(closing) | set(other)):
        groups.setdefault(group(key), []).append(key)
    rows.append(f'## {title}\n\n| Artifacts | Identical / compared | Differ | Only in one |\n| --- | --- | --- | --- |')
    for g, keys in groups.items():
        both = [k for k in keys if k in closing and k in other]
        same = [k for k in both if closing[k] == other[k]]
        differ = [k.split('/')[-1] for k in both if closing[k] != other[k]]
        only = [k.split('/')[-1] + (' (closing)' if k in closing else ' (other)') for k in keys if (k in closing) != (k in other)]
        rows.append(f"| {g} | {len(same)}/{len(both)} | {', '.join(differ[:12]) + (' ...' if len(differ) > 12 else '') or '-'} | {', '.join(only[:8]) + (' ...' if len(only) > 8 else '') or '-'} |")
    return '\n'.join(rows) + '\n'


out = ['Copyright (c) 2026 Jeremy Ellis and contributors\nSPDX-License-Identifier: Apache-2.0\n',
       '# Closing build: reproducibility, information only\n',
       'The C27 tables and the deleted build were made from other source states (the code changed since C27, and the '
       'deleted tree was built from 49cd3a4a plus uncommitted changes), so a difference here is expected wherever a '
       'source changed and is not a verdict.\n']
for name in ['F-a1', 'F-bb']:
    data = json.loads((REPO/f'native/work/repro-20261114/tables/{name}.json').read_text())['files']
    out.append(table(f'Against C27 scratch table {name} (build-tree keys)', {k: v for k, v in data.items() if k.startswith('build/')}))
final = {}
for line in (REPO/'native/work/partD-20261114/final/hashes.txt').read_text().splitlines():
    digest, path = line.split()
    final['build/host/lib/libcodebrix_cycles_host.dylib' if path.endswith('.dylib') else 'build/runner'] = digest
out.append('## Against the batch-final build (native/work/partD-20261114/final/hashes.txt)\n\n' + '\n'.join(
    f"- {k}: closing `{closing[k]}`, batch-final `{v}`: {'IDENTICAL' if closing[k] == v else 'differ'}" for k, v in final.items()) + '\n')
pre = json.loads((R/'pre-delete-build-hashes.json').read_text())['files']
out.append(table('Against the build tree deleted at the start of phase 1', pre))
print('\n'.join(out))
