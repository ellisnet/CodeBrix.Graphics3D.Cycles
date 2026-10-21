#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 3 copy (via merge wave 1) of the Phase D5 freeze script. Freeze the session-close (merge wave 3) evidence checkpoint (C1/C closeout policy: no compiled
binaries, *.bin films and files over 1 MB recorded by sha256 and work location)."""
import datetime, gzip, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
REPO = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
OUT = REPO/'native/evidence/session-close-20261020'
WT = Path('/home/jeremy/ClaudeHome/cycles-worktrees')
SOURCES = [  # (label in checkpoint, absolute folder, location string)
    ('laneE4', WT/'e4-mode4-adreno/native/work/e4/handoff', 'worktree e4-mode4-adreno: native/work/e4/handoff'),
    ('laneE5', WT/'e5-pixel-sampler/native/work/e5/handoff', 'worktree e5-pixel-sampler: native/work/e5/handoff'),
    ('laneM4', WT/'m4-bump-derivative/native/work/m4/handoff', 'worktree m4-bump-derivative: native/work/m4/handoff'),
    ('merge3', REPO/'native/work/merge3', 'native/work/merge3'),
]
SKIP_DIRS = {'bin', 'obj', '__pycache__', 'cycles-build', 'TestResults', 'pack', 'cycles-source', 'build', 'dependencies', 'build-android-arm64', 'build-android-x64', 'replay-build', 'probe-build', 'diag-tree', 'diag-e4-build-android-arm64'}
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
        if name.startswith('native/evidence/session-close-20261020/'): continue
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
        scope='Session close (merge wave 3): Phase E5 (sliced Pixel sampler dispatch), Phase E4 (contraction-proof ShaderData position/object transform, reference-oracle arithmetic in reference-only gates) and M4 (bump stages and every derivative opcode, bits 55-61) merged into main on top of Phase D and merge waves 1-2; M4 probe hook replaced by the scene-specialized probe; specialization-off check partial by decision (DEVICE.md open item 11)',
        webGpuPackage='CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433',
        reconstruction='Folders laneE4, laneE5 and laneM4 mirror the sealed handoff folders and merge3 mirrors native/work/merge3 (pre/post-merge manifests, reconciliation, validation, device runs). Mesa caches, build trees, bin/obj and pack outputs are excluded; compiled binaries are refused (refused-binaries.json, by sha256 and location); *.bin/*.f32 films, files over 1 MB and the staged notice copies of the Android app build are listed in external-payloads.json by sha256, size and work location; WGSL modules are stored once each, gzip-compressed, under wgsl-modules/ (wgsl-modules.json maps every original path). final-source-sha256.json: every tracked and untracked non-ignored repository file at freeze time; final-binary-sha256.json: Linux and Android native build outputs.',
        sources={label: location for label, _, location in SOURCES},
        externalPayloads=len(external), wgslFiles=len(wgsl), refusedBinaries=len(refused), savedSha256=saved)
    (OUT/'manifest.json').write_text(json.dumps(manifest, indent=1, sort_keys=True))
    mismatches = sum(1 for k, v in saved.items() if sha(OUT/k) != v)
    elf = sum(1 for p in OUT.rglob('*') if p.is_file() and is_elf(p))
    size = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    print(json.dumps(dict(files=len(saved) + 1, bytes=size, mismatches=mismatches, elfFiles=elf, external=len(external), refused=len(refused),
                          manifestSha256=sha(OUT/'manifest.json'))))

if __name__ == '__main__': main()
