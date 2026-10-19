#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Freeze the bounded image gate before extending the renderer toward parity."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; WORK=ROOT/'work'
OUT=ROOT/'evidence/scene-image'; OUT.mkdir(parents=True,exist_ok=True)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def copy(source,dest):
    dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,dest)
for name in ['scene.bin','scene.json','reference.json','reference-build.json',
             'host-build-commands.json','webgpu-compatibility.json']:
    copy(WORK/name,OUT/name)
copy(WORK/'generated/render.wgsl',OUT/'render.wgsl')
copy(ROOT.parent/'research/THIRD-PARTY-NOTICES.txt',OUT/'THIRD-PARTY-NOTICES.txt')
copy(ROOT.parent/'LICENSE',OUT/'LICENSE')
manifest={'workOutputs':{},'sources':{},'gate':'Restricted native scene-to-image; not full Cycles parity'}
for folder,label in [('linux-x64','linux-x64'),('android-98271FFBA0048D','pixel-api33'),
                     ('android-192.168.86.225_5555','hp-api33'),('android-R52W207XQ1V','samsung-api36')]:
    source=WORK/folder
    if not json.loads((source/'validation.json').read_text())['passed']: raise RuntimeError('Failed image gate: '+folder)
    for name in ['render.json','render.adapter.txt','render.library.txt','render.png','validation.json',
                 'device.json','webgpu-deployment.json']:
        if (source/name).exists(): copy(source/name,OUT/label/name)
    manifest['workOutputs'][folder+'/render.rgba.bin']=sha(source/'render.rgba.bin')
for rid in ['linux-x64','android-arm64','android-x64']:
    copy(WORK/rid/'build.json',OUT/('build-'+rid+'.json'))
manifest['workOutputs']['reference.rgba.bin']=sha(WORK/'reference.rgba.bin')
for directory in [ROOT/'native',ROOT/'shaders',ROOT/'tools',ROOT.parent/'research/native']:
    for path in sorted(directory.rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            manifest['sources'][str(path.relative_to(ROOT.parent))]=sha(path)
import sys
sys.path.insert(0,str(ROOT.parent/'tools'))
from cycles_source import verify as verify_cycles, REVISION
verify_cycles()
manifest['cyclesRevision']=REVISION
manifest['filteredSourceManifestSha256']=sha(ROOT.parent/'third_party/cycles-import.json')
ldd=subprocess.check_output(['ldd',str(WORK/'cycles-build/bin/codebrix_cycles_host_probe')],text=True)
if 'libjbig' in ldd or 'libtiff' in ldd: raise RuntimeError('Unexpected TIFF/JBIG dependency')
(OUT/'host-runtime-dependencies.txt').write_text(ldd)
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(OUT)
