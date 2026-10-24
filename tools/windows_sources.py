# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Windows-side verification of the reviewed dependency snapshot.

tools/licensing/dependency_sources.py verify() compares its manifest's '/'
paths with str() of on-disk paths, which on Windows contain '\\'. That file is
the reviewed import policy: its own SHA-256 is pinned in
third_party/dependency-imports.json (importerSha256) and in
third_party/license-review-exceptions.json, so editing it would invalidate the
review. The Windows recipes call this module instead. It applies the same checks
(the importer is unchanged, every listed file has its reviewed SHA-256, no
symlinks, no unlisted files) with '/' paths, and on Linux and macOS its result is
identical to dependency_sources.verify().
"""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'third_party/deps'
MANIFEST=ROOT/'third_party/dependency-imports.json'
IMPORTER=ROOT/'tools/licensing/dependency_sources.py'


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_dependencies():
 m=json.loads(MANIFEST.read_text())
 if m['importerSha256']!=digest(IMPORTER):raise RuntimeError('Dependency import policy changed without review')
 expected=set()
 for c in m['components']:
  for f in c['files']:
   name=c['id']+'/'+f['path'];expected.add(name);path=DEST/name
   if path.is_symlink() or not path.is_file() or digest(path)!=f['sha256']:raise RuntimeError('Unreviewed dependency source change: '+name)
 actual={p.relative_to(DEST).as_posix() for p in DEST.rglob('*') if p.is_file()}
 if actual!=expected:raise RuntimeError('Unreviewed dependency files: '+str(sorted(actual^expected)[:20]))
 return m


if __name__=='__main__':
 print(len(verify_dependencies()['components']),'reviewed dependency components verified')
