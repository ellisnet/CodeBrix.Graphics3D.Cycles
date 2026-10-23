#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Re-point a copied CMake build tree at the current repository root.

A lane worktree may be seeded with a copy of another checkout's build trees
(native/work/cycles-build, prototype/work/cycles-build,
native/work/android/<rid>/host/build). The copy still names the other
checkout's absolute paths (CMakeCache, Makefiles, compiler depfiles), and the
freshly checked-out sources are newer than the copied objects, so a plain build
either refuses (CMake cache directory mismatch) or recompiles everything.

rebase() makes the copy behave as if it had been built in this checkout:
1. The old root is read from the cache (CMAKE_CACHEFILE_DIR minus the build
   folder's path relative to the new root). Nothing happens when it already
   is the new root.
2. CMake places objects of sources outside the CMake source tree (native/cycles,
   the generated overlay) under <target>.dir/<absolute source path>; those
   object directories are moved from the old root's path to the new root's
   path (times kept), so the objects are found again.
   Every text file in the build tree (not ELF objects, archives or other
   binaries) has the old root replaced by the new root; each rewritten file
   keeps its modification time, so Make sees no changed flags or rules.
3. Every file outside the build tree that the build files reference under the
   new root (sources, headers, generated overlays, dependency prefixes,
   CMakeLists) takes the modification time of the same path in the reference
   checkout (default: the old root) when the two files are byte-identical. A
   file that differs (edited in this checkout, or changed in the reference since
   the copy) keeps its newer time and rebuilds, as do its dependants. Make then
   reaches the same verdict the reference checkout would, plus the edits.
   Referenced generated files under a work/ folder (the copied dependency
   prefix's CMake package files, the generated overlay header that includes
   native/cycles/device.h) that name the old root are rewritten first, keeping
   their times; they then count as identical when they equal the reference
   file with the same rewrite. Tracked sources are never rewritten.
4. verify() checks that no text file in the tree still names the old root and
   that every compile_commands.json source is under the new root. Callers run
   it again after their normal CMake configure step.

The build scripts expose this as --rebase-build-tree (tools/build.py,
native/tools/build_cycles.py, prototype/tools/build_host.py,
native/tools/build_cycles_android.py). Usage of this module alone:
  rebase_build_tree.py BUILD_DIR [--root ROOT] [--reference ROOT] [--report FILE]
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_BINARY_MAGIC = (b'\x7fELF', b'!<arch>\n', b'\x89PNG', b'PK\x03\x04')


def cache_root(build, root=ROOT):
    """The checkout root the build tree was configured in (from CMakeCache.txt)."""
    build, root = Path(build).resolve(), Path(root).resolve()
    text = (build/'CMakeCache.txt').read_text()
    match = re.search(r'^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$', text, re.M)
    if not match:
        raise RuntimeError(f'{build}: CMakeCache.txt has no CMAKE_CACHEFILE_DIR')
    cached = match.group(1).strip()
    relative = str(build.relative_to(root))
    if cached != relative and not cached.endswith('/'+relative):
        raise RuntimeError(f'{build}: cache directory {cached} is not <root>/{relative}; not a copy of this layout')
    return cached[:len(cached)-len(relative)].rstrip('/')


def _is_text(data):
    return not data.startswith(_BINARY_MAGIC) and b'\0' not in data[:65536]


def _pattern(root):
    # The root followed by a path separator or a non-path character, never a longer name.
    return re.compile(re.escape(root.encode())+rb'(?=[/\s"\';:)\]}>,=\\]|$)', re.M)


def _text_files(build):
    for path in sorted(build.rglob('*')):
        if path.is_file() and not path.is_symlink():
            yield path


def _move_object_directories(build, old, new):
    """Moves <dir>/<old root without '/'> to <dir>/<new root without '/'> wherever it occurs in BUILD."""
    old_rel, new_rel, moved = old.lstrip('/'), new.lstrip('/'), 0
    found = sorted({str(p)[:-len(old_rel)-1] for p in build.rglob(Path(old_rel).name)
                    if p.is_dir() and str(p).endswith('/'+old_rel)}, key=len)
    for parent in found:
        source, target = Path(parent)/old_rel, Path(parent)/new_rel
        if not source.is_dir():
            continue
        if target.exists():
            raise RuntimeError(f'{target} already exists; refusing to merge object directories')
        stamp = source.stat()
        holding = Path(parent)/('.rebase-'+str(os.getpid()))
        source.rename(holding)            # also safe when the new root lies under the old one
        target.parent.mkdir(parents=True, exist_ok=True)
        holding.rename(target)
        os.utime(target, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        empty = source.parent
        while empty != Path(parent) and empty.is_dir() and not any(empty.iterdir()):
            empty.rmdir()
            empty = empty.parent
        moved += 1
    return moved


def verify(build, root=ROOT, old=None):
    """Raises unless the tree names only ROOT (and never OLD) and every compiled source is under ROOT."""
    build, root = Path(build).resolve(), Path(root).resolve()
    problems = []
    if cache_root(build, root) != str(root):
        problems.append('CMAKE_CACHEFILE_DIR is not under '+str(root))
    if old and old != str(root):
        pattern = _pattern(old)
        for path in _text_files(build):
            # link.d is written by the linker: copied dependency libraries carry the
            # old prefix in their binary RUNPATH, which the linker follows for
            # indirect libraries. Those are library lookups, not sources.
            if path.name == 'link.d':
                continue
            data = path.read_bytes()
            if _is_text(data) and pattern.search(data):
                problems.append(f'{path.relative_to(build)} still names {old}')
    if old and old != str(root):
        leftovers = [p for p in build.rglob(Path(old).name) if p.is_dir() and str(p).endswith(old)]
        problems += [f'{p.relative_to(build)}: object directory still under the old root path' for p in leftovers]
    commands = build/'compile_commands.json'
    if commands.exists():
        for row in json.loads(commands.read_text()):
            source = Path(row['file']) if os.path.isabs(row['file']) else Path(row['directory'])/row['file']
            if not str(source.resolve()).startswith(str(root)+'/'):
                problems.append('compile source outside the root: '+str(source))
    if problems:
        raise RuntimeError('build tree re-rooting check failed:\n  '+'\n  '.join(problems[:40]))
    return True


def rebase(build, root=ROOT, reference=None, log=print):
    """Re-points BUILD (a copied CMake build tree under ROOT); returns a report dict."""
    build, root = Path(build).resolve(), Path(root).resolve()
    old = cache_root(build, root)
    report = dict(build=str(build), root=str(root), oldRoot=old, rebased=False)
    if old == str(root):
        log(f'{build}: already configured in {root}; nothing to re-root')
        return report
    reference = Path(reference or old)
    if not reference.is_dir():
        raise RuntimeError(f'reference checkout {reference} is missing; source times cannot be matched safely')
    pattern, new = _pattern(old), str(root).encode()
    moved = _move_object_directories(build, old, str(root))
    rewritten, referenced = 0, set()
    path_pattern = re.compile(re.escape(str(root).encode())+rb'/[A-Za-z0-9_.+@/-]+')
    for path in _text_files(build):
        data = path.read_bytes()
        if not _is_text(data):
            continue
        if pattern.search(data):
            stat = path.stat()
            data = pattern.sub(new, data)
            path.write_bytes(data)
            os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
            rewritten += 1
        referenced.update(m.group(0).decode().rstrip('.:/') for m in path_pattern.finditer(data))
    matched, differing, missing, generated = 0, [], 0, 0
    for name in sorted(referenced):
        target = Path(name)
        if not target.is_file() or target.is_symlink() or str(target).startswith(str(build)+'/'):
            continue
        relative = target.relative_to(root)
        work_file = 'work' in relative.parts[:-1]
        data = target.read_bytes()
        if work_file and _is_text(data) and pattern.search(data):
            stat = target.stat()
            data = pattern.sub(new, data)
            target.write_bytes(data)
            os.utime(target, ns=(stat.st_atime_ns, stat.st_mtime_ns))
            generated += 1
        source = reference/relative
        if not source.is_file():
            missing += 1
            continue
        expected = source.read_bytes()
        if work_file and _is_text(expected):
            expected = pattern.sub(new, expected)
        if expected == data:
            stat = source.stat()
            os.utime(target, ns=(stat.st_atime_ns, stat.st_mtime_ns))
            matched += 1
        else:
            differing.append(str(relative))
    verify(build, root, old)
    report.update(rebased=True, movedObjectDirectories=moved, rewrittenFiles=rewritten, rewrittenGeneratedFiles=generated, referencedFiles=len(referenced), timesMatched=matched,
                  differingFiles=differing, missingInReference=missing, reference=str(reference))
    log(f'{build}: re-rooted from {old}: {moved} object directories moved, {rewritten} build files and {generated} generated inputs rewritten (times kept), {matched} referenced files '
        f'took the reference times, {len(differing)} differ and will rebuild ({", ".join(differing[:8])}'
        f'{" ..." if len(differing) > 8 else ""})')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('build', type=Path)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = rebase(args.build, args.root, args.reference)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    sys.exit(main())
