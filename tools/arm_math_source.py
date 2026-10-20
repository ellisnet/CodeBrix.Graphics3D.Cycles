# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Verify the reviewed MIT scalar-math source subset without fetching code."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify():
    manifest = json.loads((ROOT/'third_party/arm-math-import.json').read_text())
    if manifest['selectedLicense'] != 'MIT':
        raise RuntimeError('Unreviewed scalar-math license selection')
    base = ROOT/'third_party/arm-optimized-routines'
    expected = {row['path']:row for row in manifest['files']}
    required = {'LICENSE', 'math/sinf.c', 'math/cosf.c', 'math/sincosf.h', 'math/sincosf_data.c'}
    actual = {str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
    if set(expected) != required or actual != required:
        raise RuntimeError('Unreviewed scalar-math source set')
    for name, row in expected.items():
        path = base/name
        if path.is_symlink() or row['license'] != 'MIT' or not row['licenseBasis']:
            raise RuntimeError('Unreviewed scalar-math provenance: '+name)
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
            raise RuntimeError('Changed scalar-math source: '+name)
    return manifest


if __name__ == '__main__':
    print(len(verify()['files']), 'reviewed MIT scalar-math files verified')
