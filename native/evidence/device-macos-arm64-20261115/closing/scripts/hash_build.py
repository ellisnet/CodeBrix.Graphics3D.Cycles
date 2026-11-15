#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""SHA-256 of the osx-arm64 build tree, keyed like native/work/repro-20261114/tools/collect.py ('build/...'):
hash_build.py OUT.json. Host library, lifecycle/reference executables, Cycles static libraries, the staged kernel
closure, the dependency prefix's dylibs and static libraries, the prefix record, the oracle runner, and the host's
dependency closure as status.json lists it (closure/<install name>)."""
import hashlib, json, sys
from pathlib import Path
base = Path('/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles/native/work/macos/osx-arm64')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
files = {}
for d in ['bin', 'lib']:
    for p in sorted((base/'host/build'/d).glob('*')):
        if p.is_file() and not p.is_symlink(): files['build/host/'+d+'/'+p.name] = sha(p)
kernel = base/'host/build/lib/codebrix-cycles'
for p in sorted(kernel.rglob('*')):
    if p.is_file(): files['build/kernel/'+str(p.relative_to(kernel))] = sha(p)
lib = base/'dependencies/install/lib'
for p in sorted(lib.rglob('*')):
    if p.is_file() and not p.is_symlink() and p.suffix in ('.dylib', '.a'): files['build/prefix/'+str(p.relative_to(lib))] = sha(p)
files['build/prefix-marker'] = sha(base/'dependencies/install/codebrix-osx-arm64-build.json')
files['build/runner'] = sha(base/'oracle-build/codebrix_cycles_oracle')
status = json.loads((base/'host/status.json').read_text())
closure = {name: sha(base/'dependencies/install'/rel) for name, rel in status['dependencyClosure'].items()}
digest = hashlib.sha256()
for k in sorted(files):
    if k.startswith('build/kernel/'): digest.update((k+' '+files[k]+'\n').encode())
out = dict(files=files, closure=closure, closureFiles=status['dependencyClosure'],
           kernelTreeFiles=sum(k.startswith('build/kernel/') for k in files), kernelTreeDigest=digest.hexdigest())
Path(sys.argv[1]).write_text(json.dumps(out, indent=1)+'\n')
print(sys.argv[1], len(files), 'files,', len(closure), 'closure dylibs,', out['kernelTreeFiles'], 'kernel files')
