#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compare the bounded native trig adapter with independent platform libm calls."""
import argparse
import concurrent.futures
import ctypes
import ctypes.util
import json
import math
import platform
import random
import shutil
import struct
import tempfile
from pathlib import Path
from build import ROOT, WORK, REPO
from arm_math_source import verify
from device_gate import sha, save
from render_gate import DEVICES
import arena_replay
import trig_gate

OUT = WORK/'native-trig'
SOURCES = [ROOT/'shaders'/name for name in ('wavefront_float.wgsl', 'wavefront_trig.wgsl', 'wavefront_native_trig.wgsl')]


def capture():
    verify()
    # Reuse the original corpus, preserving its independent binary64 gate in its
    # own directory. This separate adapter targets scalar sinf/cosf behavior.
    previous = trig_gate.OUT
    try:
        trig_gate.OUT = OUT
        trig_gate.capture()
    finally:
        trig_gate.OUT = previous
    out = OUT/'capture'
    meta = json.loads((out/'run.capture.json').read_text())
    angles = list(struct.unpack('<'+str(meta['count'])+'f', (out/'run.arena.bin').read_bytes()[256:256+4*meta['count']]))
    binary32 = lambda x:struct.unpack('<f', struct.pack('<f', x))[0]
    angles.append(struct.unpack('<f', struct.pack('<I', 0x3f5592dd))[0])
    for boundary in (-math.pi/4, 3*math.pi/4):
        bits = struct.unpack('<I', struct.pack('<f', boundary))[0]
        angles += [struct.unpack('<f', struct.pack('<I', bits-i))[0] for i in range(129)]
    for seed in (20261020, 20261021, 20261022):
        rng = random.Random(seed)
        angles += [binary32(-math.pi/4+math.pi*rng.random()) for _ in range(16384)]
    libm = ctypes.CDLL(ctypes.util.find_library('m'))
    for name in ('sinf', 'cosf'):
        function = getattr(libm, name)
        function.argtypes = [ctypes.c_float]
        function.restype = ctypes.c_float
    # Record the actual Linux oracle library, not just its soname.
    libraries = {Path(line.split()[-1]).resolve() for line in Path('/proc/self/maps').read_text().splitlines()
                 if '/libm.so' in line or '/libm-' in line}
    assert len(libraries) == 1, 'Ambiguous platform libm oracle'
    library = libraries.pop()
    count = len(angles); offset = 256+count*4
    (out/'run.arena.bin').write_bytes(bytes(256)+struct.pack('<'+str(count)+'f', *angles)+bytes(count*8+256))
    (out/'run.reference.bin').write_bytes(b''.join(struct.pack('<2f', libm.sinf(a), libm.cosf(a)) for a in angles))
    params = [0]*32;params[1] = count;params[4] = offset
    (out/'run.params.bin').write_bytes(struct.pack('<32I', *params))
    meta.update(count=count, outputOffset=offset)
    save(out/'run.capture.json', meta)
    shader = '''// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND MIT
@group(0) @binding(0) var<storage,read_write> heap:array<u32>;
@group(0) @binding(1) var<uniform> params:array<vec4<u32>,8>;
fn p(i:u32)->u32{return params[i/4u][i%4u];}
''' + ''.join(p.read_text() for p in SOURCES) + '''
@compute @workgroup_size(64) fn trig_probe(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=p(1u)){return;}
    let sc=native_disk_sincos(bitcast<f32>(heap[64u+i]));let offset=p(4u)/4u+2u*i;
    heap[offset]=bitcast<u32>(sc.x);heap[offset+1u]=bitcast<u32>(sc.y);
}
'''
    (out/'run.wgsl').write_text(shader)
    save(out/'generation.json', {'angles':count, 'exactChecksPerDevice':2*count,
                                'sourceSha256':{str(p.relative_to(REPO)):sha(p) for p in SOURCES},
                                'generatorSha256':sha(Path(__file__)),
                                'sourceManifestSha256':sha(REPO/'third_party/arm-math-import.json'),
                                'oracle':'Independent platform sinf/cosf calls; not the WGSL polynomial',
                                'oraclePlatform':platform.platform(), 'oracleLibrary':str(library),
                                'oracleLibrarySha256':sha(library),
                                'scope':'Bounded disk-angle native trig adapter; no full renderer or native Android host claim.'})


def replay():
    generation = json.loads((OUT/'capture/generation.json').read_text())
    sources = {str(p.relative_to(REPO)):sha(p) for p in SOURCES}
    assert generation['sourceSha256'] == sources, 'Native trig capture predates current shaders'
    assert generation['generatorSha256'] == sha(Path(__file__)), 'Native trig generator changed'
    assert generation['sourceManifestSha256'] == sha(REPO/'third_party/arm-math-import.json'), 'Scalar math source pin changed'
    inputs = OUT/'replay-inputs';inputs.mkdir(parents=True, exist_ok=True)
    fixed = Path(tempfile.mkdtemp(prefix='capture-', dir=inputs))
    shutil.copytree(OUT/'capture', fixed, dirs_exist_ok=True)
    pins = {p.name:sha(p) for p in fixed.iterdir() if p.is_file()}
    arena_replay.configure(fixed, OUT/'replay', '/data/local/tmp/codebrix-native-trig')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(arena_replay.build, rid) for rid in sorted({d[0] for d in DEVICES})]:
            future.result()
    errors = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        jobs = [(label, pool.submit(arena_replay.run, rid, serial)) for rid,serial,label in DEVICES]
        for label, future in jobs:
            try:
                future.result()
            except Exception as error:
                errors[label] = str(error)
    assert all(sha(fixed/name) == digest for name,digest in pins.items()), 'Fixed native trig capture changed'
    assert all(sha(REPO/name) == digest for name,digest in sources.items()), 'Native trig source changed during replay'
    report = {'passed':not errors, 'errors':errors, 'capture':str(fixed), 'inputSha256':pins}
    save(OUT/'batch.json', report)
    if errors:
        raise RuntimeError('Native trig replay failures: '+json.dumps(errors))
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['capture', 'replay', 'run'])
    args = parser.parse_args()
    if args.action in ('capture', 'run'):
        capture()
    if args.action in ('replay', 'run'):
        replay()
