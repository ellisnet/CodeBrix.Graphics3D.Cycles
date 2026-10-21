#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 2 copy (via merge wave 1) of the Phase D5 freeze script. Freeze the materials merge (wave 2) evidence checkpoint (C1/C closeout policy: no compiled
binaries, *.bin films and files over 1 MB recorded by sha256 and work location)."""
import datetime, gzip, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
REPO = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
OUT = REPO/'native/evidence/materials-merge-20261020'
WT = Path('/home/jeremy/ClaudeHome/cycles-worktrees')
SOURCES = [  # (label in checkpoint, absolute folder, location string)
    ('laneM1', WT/'m1-principled/native/work/m1/handoff', 'worktree m1-principled: native/work/m1/handoff'),
    ('laneM2', WT/'m2-svm-nodes/native/work/m2/handoff', 'worktree m2-svm-nodes: native/work/m2/handoff'),
    ('laneM3', WT/'m3-procedural/native/work/m3/handoff', 'worktree m3-procedural: native/work/m3/handoff'),
    ('merge2', REPO/'native/work/merge2', 'native/work/merge2'),
]
SKIP_DIRS = {'bin', 'obj', '__pycache__', 'cycles-build', 'TestResults', 'pack', 'cycles-source', 'build', 'dependencies', 'build-android-arm64', 'build-android-x64', 'replay-build', 'probe-build', 'diag-tree'}
BINARY_SUFFIX = {'.so', '.dll', '.exe', '.apk', '.aab', '.nupkg', '.a', '.o', '.dex', '.jar', '.pdb', '.zip', '.idsig'}
LIMIT = 1 << 20

def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()

def is_elf(p):
    with open(p, 'rb') as f: return f.read(4) == b'\x7fELF'

def main():
    assert not OUT.exists(), OUT
    OUT.mkdir(parents=True)
    external, refused, saved, wgsl = {}, [], {}, {}
    for label, folder, location in SOURCES:
        if not folder.is_dir(): continue
        for root, dirs, files in os.walk(folder):
            rel_root = Path(root).relative_to(folder)
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith('mesa'))
            for name in sorted(files):
                src = Path(root)/name
                if src.is_symlink() or not src.is_file(): continue
                rel = Path(label)/rel_root/name
                if is_elf(src) or src.suffix.lower() in BINARY_SUFFIX or name.endswith('-Signed.apk'):
                    refused.append(dict(path=str(rel), sha256=sha(src), bytes=src.stat().st_size, workLocation=location+'/'+str(rel_root/name)))
                    continue
                if src.suffix == '.wgsl':
                    digest = sha(src)
                    module = OUT/'wgsl-modules'/(digest+'.wgsl.gz')
                    if not module.exists():
                        module.parent.mkdir(parents=True, exist_ok=True)
                        with open(src, 'rb') as f, open(module, 'wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=0) as g: g.write(f.read())
                    wgsl[str(rel)] = dict(sha256=digest, bytes=src.stat().st_size, module='wgsl-modules/'+digest+'.wgsl.gz', workLocation=location+'/'+str(rel_root/name))
                    continue
                # Staged notice copies inside the Android app build (repository and SDK notices,
                # verbatim) are recorded by hash: the source audit reads evidence text for
                # license wording, and these are copies of files kept elsewhere.
                if src.suffix in ('.bin', '.f32') or src.stat().st_size > LIMIT or '/app/notices/' in '/'+str(rel)+'/':
                    external[str(rel)] = dict(sha256=sha(src), bytes=src.stat().st_size, workLocation=location+'/'+str(rel_root/name))
                    continue
                dst = OUT/rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
    # Final source hashes (tracked and untracked, not ignored) and binary hashes.
    listed = subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=REPO).split(b'\0')
    final = {}
    for raw in listed:
        if not raw: continue
        name = raw.decode()
        if name.startswith('native/evidence/materials-merge-20261020/'): continue
        p = REPO/name
        if p.is_file(): final[name] = sha(p)
    (OUT/'final-source-sha256.json').write_text(json.dumps(final, indent=1, sort_keys=True))
    binaries = {}
    for folder in [REPO/'native/work/cycles-build/lib', REPO/'native/work/cycles-build/bin',
                   REPO/'native/work/android/android-arm64/host/build/lib', REPO/'native/work/android/android-arm64/host/build/bin',
                   REPO/'native/work/android/android-x64/host/build/lib', REPO/'native/work/android/android-x64/host/build/bin']:
        for p in sorted(folder.glob('*')):
            if p.is_file() and (p.name.startswith('codebrix_') or p.name.startswith('libcodebrix')):
                binaries[str(p.relative_to(REPO))] = sha(p)
    (OUT/'final-binary-sha256.json').write_text(json.dumps(binaries, indent=1, sort_keys=True))
    (OUT/'external-payloads.json').write_text(json.dumps(external, indent=1, sort_keys=True))
    (OUT/'refused-binaries.json').write_text(json.dumps(refused, indent=1))
    (OUT/'wgsl-modules.json').write_text(json.dumps(wgsl, indent=1, sort_keys=True))
    for p in sorted(OUT.rglob('*')):
        if p.is_file(): saved[str(p.relative_to(OUT))] = sha(p)
    manifest = dict(capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        cyclesRevision='a456b761034dda42c32eef9f4aae0fa5a5c9f604',
        scope='Materials merge (wave 2): the combined M1 (Principled BSDF), M2 (texture, colour and utility SVM nodes) and M3 (reconciliation, wavelength, wireframe; Gabor withheld) result merged into main on top of merge wave 1; SVM probe gates moved to scene-specialized modules (Intel Mesa 25.0.7 miscompile of the module-scope svm_stack in the whole unspecialized probe module) with an unspecialized compile smoke step; Linux validation and materials device validation on HP Android x64, Samsung SM-X700 and Pixel 4 XL.',
        webGpuPackage='CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433',
        reconstruction='Folders laneM1..laneM3 mirror the sealed handoff folders and merge2 mirrors native/work/merge2 (pre/post-merge manifests, reconciliation, validation, device runs). Mesa caches, build trees, bin/obj and pack outputs are excluded; compiled binaries are refused (refused-binaries.json, by sha256 and location); *.bin/*.f32 films, files over 1 MB and the staged notice copies of the Android app build are listed in external-payloads.json by sha256, size and work location; WGSL modules are stored once each, gzip-compressed, under wgsl-modules/ (wgsl-modules.json maps every original path). final-source-sha256.json: every tracked and untracked non-ignored repository file at freeze time; final-binary-sha256.json: Linux and Android native build outputs.',
        sources={label: location for label, _, location in SOURCES},
        externalPayloads=len(external), wgslFiles=len(wgsl), refusedBinaries=len(refused), savedSha256=saved)
    (OUT/'manifest.json').write_text(json.dumps(manifest, indent=1, sort_keys=True))
    mismatches = sum(1 for k, v in saved.items() if sha(OUT/k) != v)
    elf = sum(1 for p in OUT.rglob('*') if p.is_file() and is_elf(p))
    size = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    print(json.dumps(dict(files=len(saved) + 1, bytes=size, mismatches=mismatches, elfFiles=elf, external=len(external), refused=len(refused),
                          manifestSha256=sha(OUT/'manifest.json'))))

if __name__ == '__main__': main()
