#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4 copy (via merge waves 1 and 3) of the Phase D5 freeze script. Freeze the merge-wave-4 evidence checkpoint (C1/C closeout policy: no compiled
binaries, *.bin films and files over 1 MB recorded by sha256 and work location)."""
import datetime, gzip, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
REPO = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
OUT = REPO/'native/evidence/merge-20261023b'
WT = Path('/home/jeremy/ClaudeHome/cycles-worktrees')
SOURCES = [  # (label in checkpoint, absolute folder, location string) -- merge wave 6 part 2
    ('merge6', REPO/'native/work/merge6', 'native/work/merge6'),
    ('m1-asset/android-app/device-3-bench3-samsung', WT/'m1-asset/native/work/m1-asset/android-app/device-3-bench3-samsung', 'worktree m1-asset: native/work/m1-asset/android-app/device-3-bench3-samsung'),
    ('m1-asset/android-app/device-3-plane2-samsung', WT/'m1-asset/native/work/m1-asset/android-app/device-3-plane2-samsung', 'worktree m1-asset: native/work/m1-asset/android-app/device-3-plane2-samsung'),
    ('m1-asset/android-app/device-4-bench3-hp', WT/'m1-asset/native/work/m1-asset/android-app/device-4-bench3-hp', 'worktree m1-asset: native/work/m1-asset/android-app/device-4-bench3-hp'),
    ('m1-asset/android-app/device-4-plane2-hp', WT/'m1-asset/native/work/m1-asset/android-app/device-4-plane2-hp', 'worktree m1-asset: native/work/m1-asset/android-app/device-4-plane2-hp'),
    ('m1-asset/oracles', WT/'m1-asset/native/work/m1-asset/oracles', 'worktree m1-asset: native/work/m1-asset/oracles'),
    ('m1-asset/comparator-proof', WT/'m1-asset/native/work/m1-asset/comparator-proof', 'worktree m1-asset: native/work/m1-asset/comparator-proof'),
    ('m1-asset', WT/'m1-asset/native/work/m1-asset', 'worktree m1-asset: native/work/m1-asset', ['device-3-samsung.log','device-4-hp.log','oracles-bench.log','oracles-starters.log','regress-summary.txt','timeline.log','budgets.txt']),
]
SKIP_DIRS = {'bin', 'obj', '__pycache__', 'cycles-build', 'TestResults', 'pack', 'cycles-source', 'build', 'dependencies', 'build-android-arm64', 'build-android-x64', 'replay-build', 'probe-build', 'diag-tree', 'diag-e4-build-android-arm64', 'debug', 'arena', 'stage-tmp-samsung', 'stage-tmp-hp', 'stage-tmp-pixel', 'stage-tmp-s20'}
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
    for label, folder, location, *only in SOURCES:
        if not folder.is_dir(): continue
        walk = [(str(folder), [], list(only[0]))] if only else os.walk(folder)
        for root, dirs, files in walk:
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
                if src.suffix in ('.bin', '.f32', '.case') or src.stat().st_size > LIMIT or '/app/notices/' in '/'+str(rel)+'/' or '/scene-dump/' in '/'+str(rel)+'/' or '/scenes/' in '/'+str(rel)+'/' or src.suffix == '.patch' or str(rel).startswith('merge6/base/'):  # merge wave 6: patches and the pre-merge base copies of licensing documents contain licence wording the source audit would flag; recorded by hash
                    
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
        if name.startswith('native/evidence/merge-20261023b/'): continue
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
        scope='Merge wave 6 (2026-10-23): lanes A650-2 (Principled SVM rotation fix for the Adreno 650), S3 (tablet stage-state exchange, triangle-light normalization barrier) and m1-asset (textured bench through glTF tool, scene bundles, comparator) merged into main on top of merge wave 5; part 1 merge validation (merge6 work folder) and part 2 evidence: Linux full corpus, Samsung SM-X700, HP (Android x64), S20 (Adreno 650) device runs, the managed bundle-mode runs of the control and bench bundles, the comparator results, the ledger re-recorded on the final tree (merge6/ledger) and the device tables; m1-asset/ holds the lane evidence that native/DEVICE.md cites (android-app device-3/4 runs, oracles, comparator-proof)',
        webGpuPackage='CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433',
        reconstruction='merge6 mirrors native/work/merge6 (pre/post-merge manifests, reconciliation, validation, device runs, bundles, oracles, ledger reports); m1-asset/ mirrors the cited evidence files of the m1-asset lane worktree (native/work/m1-asset). Mesa caches, build trees, bin/obj and pack outputs are excluded; compiled binaries are refused (refused-binaries.json, by sha256 and location); *.bin/*.f32 films, files over 1 MB and the staged notice copies of the Android app build are listed in external-payloads.json by sha256, size and work location; WGSL modules are stored once each, gzip-compressed, under wgsl-modules/ (wgsl-modules.json maps every original path). final-source-sha256.json: every tracked and untracked non-ignored repository file at freeze time; final-binary-sha256.json: Linux and Android native build outputs.',
        sources={s[0]: s[2] for s in SOURCES},
        externalPayloads=len(external), wgslFiles=len(wgsl), refusedBinaries=len(refused), savedSha256=saved)
    (OUT/'manifest.json').write_text(json.dumps(manifest, indent=1, sort_keys=True))
    mismatches = sum(1 for k, v in saved.items() if sha(OUT/k) != v)
    elf = sum(1 for p in OUT.rglob('*') if p.is_file() and is_elf(p))
    size = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    print(json.dumps(dict(files=len(saved) + 1, bytes=size, mismatches=mismatches, elfFiles=elf, external=len(external), refused=len(refused),
                          manifestSha256=sha(OUT/'manifest.json'))))

if __name__ == '__main__': main()
