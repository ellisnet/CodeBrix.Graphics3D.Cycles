# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""The only Cycles implementation source permitted in active project builds."""
import hashlib
import json
import shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'third_party/cycles'
REVISION='a456b761034dda42c32eef9f4aae0fa5a5c9f604'


def verify():
    manifest=json.loads((ROOT/'third_party/cycles-import.json').read_text())
    importer=ROOT/'tools/licensing/import_cycles.py'
    if hashlib.sha256(importer.read_bytes()).hexdigest()!=manifest['importerSha256']:
        raise RuntimeError('Cycles import policy changed without reviewed manifest')
    if manifest['revision'] != REVISION:
        raise RuntimeError('Unexpected filtered Cycles baseline')
    expected={r['path'] for r in manifest['files']}
    # Manifest paths use '/' on every host (as_posix is str() on Linux and macOS).
    actual={p.relative_to(SOURCE).as_posix() for p in SOURCE.rglob('*') if p.is_file()}
    if actual != expected:
        raise RuntimeError('Filtered Cycles inventory changed: '+str(sorted(actual^expected)))
    for row in manifest['files']:
        path=SOURCE/row['path']
        if row.get('replacementSource') and (ROOT/row['replacementSource']).read_bytes()!=path.read_bytes():
            raise RuntimeError('Replacement template differs from vendored source: '+row['path'])
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:
            raise RuntimeError('Filtered Cycles source changed without reviewed manifest: '+row['path'])
    return SOURCE


def remove_stale_copy(copy,adapted=()):
    """A recipe's generated source copy (native/work/.../host/cycles-source) is made only when missing. One made before
    a change to the pinned import fails the recipe's own verification, so it is removed here and the recipe makes a new
    one. Files in ADAPTED are the recipe's own edits and are checked by the recipe. Returns True when it removed COPY."""
    copy=Path(copy)
    if not copy.exists():
        return False
    manifest=json.loads((ROOT/'third_party/cycles-import.json').read_text())
    expected={r['path']:r['sha256'] for r in manifest['files']}
    actual={p.relative_to(copy).as_posix() for p in copy.rglob('*') if p.is_file()}
    if actual==set(expected) and all(hashlib.sha256((copy/n).read_bytes()).hexdigest()==d for n,d in expected.items() if n not in adapted):
        return False
    print('Removing a Cycles source copy that predates the pinned import: '+str(copy),flush=True)
    shutil.rmtree(copy)
    return True
