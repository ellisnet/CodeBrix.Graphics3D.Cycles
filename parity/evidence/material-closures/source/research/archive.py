#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Snapshot reproducible research evidence; never change validation thresholds."""
import datetime
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parent
work = root/'work'
evidence = root/'evidence/native-kernels'
evidence.mkdir(parents=True, exist_ok=True)
for name in ('provenance.json','translation.json','bvh-fixture.json','cases.json',
             'translated.wgsl','numerical-analysis.json', 'build-linux-x64.json',
             'build-android-arm64.json','build-android-x64.json'):
    shutil.copy2(work/name,evidence/name)
directories = {'linux-x64':work, 'pixel-api33':work/'android-98271FFBA0048D',
               'hp-api33':work/'android-192.168.86.225_5555', 'samsung-api36':work/'android-R52W207XQ1V'}
manifest = {'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'fixtureSha256':{}, 'outputSha256':{}}
for case in json.loads((work/'cases.json').read_text()):
    for name in [case['Shader'], *case['Buffers'], case['Name']+'.expected.bin']:
        manifest['fixtureSha256'][name] = hashlib.sha256((work/name).read_bytes()).hexdigest()
for label,path in directories.items():
    destination=evidence/label; destination.mkdir(exist_ok=True)
    for name in ('native-gpu.json','validation.json','device.json'):
        if (path/name).exists(): shutil.copy2(path/name,destination/name)
    manifest['outputSha256'][label] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in path.glob('*.gpu.bin')}
(evidence/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(evidence)
