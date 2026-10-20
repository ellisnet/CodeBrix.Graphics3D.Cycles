#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Exact binary32 output checks for the bounded disk-angle adapter."""
import argparse
import concurrent.futures
import json
import math
import random
import struct
from pathlib import Path
from build import ROOT, WORK
from device_gate import sha, save
from render_gate import DEVICES
import arena_replay

OUT = WORK/'trig-rounding'


def capture():
    out = OUT/'capture'
    out.mkdir(parents=True, exist_ok=True)
    def binary32(value):
        return struct.unpack('<f', struct.pack('<f', value))[0]
    rng = random.Random(20261019)
    angles = [binary32(-math.pi/4+math.pi*rng.random()) for _ in range(16384)]
    angles += [0., -0., binary32(math.pi/4), binary32(math.pi/2),
               binary32(3*math.pi/4), binary32(-math.pi/4)]
    for middle in (.000244140625, -.000244140625, math.pi/4, math.pi/2):
        bits = struct.unpack('<I', struct.pack('<f', middle))[0]
        angles += [struct.unpack('<f', struct.pack('<I', bits+i))[0] for i in range(-128, 129)]
    count = len(angles)
    offset = 256+4*count
    arena = bytearray(offset+8*count+256)
    for i, angle in enumerate(angles):
        struct.pack_into('<f', arena, 256+4*i, angle)
    (out/'run.arena.bin').write_bytes(arena)
    # Independent binary64 libm evaluation, rounded once to binary32;
    # the expected values do not reproduce the WGSL Taylor implementation.
    (out/'run.reference.bin').write_bytes(b''.join(
        struct.pack('<2f', math.sin(a), math.cos(a)) for a in angles))
    params = [0]*32
    params[1], params[4] = count, offset
    (out/'run.params.bin').write_bytes(struct.pack('<32I', *params))
    save(out/'run.capture.json', {
        'entry':'trig_probe', 'recordSize':8, 'count':count, 'outputOffset':offset,
        'errorOffset':0, 'fields':[{'name':name, 'offset':4*i, 'size':4, 'floating':False}
                                  for i, name in enumerate(('sin_bits', 'cos_bits'))]})
    sources = [ROOT/'shaders'/name for name in ('wavefront_float.wgsl', 'wavefront_trig.wgsl')]
    shader = '''// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
@group(0) @binding(0) var<storage,read_write> heap:array<u32>;
@group(0) @binding(1) var<uniform> params:array<vec4<u32>,8>;
fn p(i:u32)->u32{return params[i/4u][i%4u];}
''' + ''.join(p.read_text() for p in sources) + '''
@compute @workgroup_size(64) fn trig_probe(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=p(1u)){return;}
    let sc=precise_disk_sincos(bitcast<f32>(heap[64u+i]));let offset=p(4u)/4u+2u*i;
    heap[offset]=bitcast<u32>(sc.x);heap[offset+1u]=bitcast<u32>(sc.y);
}
'''
    (out/'run.wgsl').write_text(shader)
    save(out/'generation.json', {'angles':count, 'exactChecksPerDevice':2*count,
                                'sourceSha256':{p.name:sha(p) for p in sources},
                                'generatorSha256':sha(Path(__file__)),
                                'scope':'Bounded disk-angle sine/cosine; not renderer parity.'})


def replay(build):
    arena_replay.configure(OUT/'capture', OUT/'replay', '/data/local/tmp/codebrix-trig-rounding')
    if build:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            for future in [pool.submit(arena_replay.build, rid) for rid in sorted({d[0] for d in DEVICES})]:
                future.result()
    inputs = {str(p):sha(p) for p in (OUT/'capture').iterdir() if p.is_file()}
    errors = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        jobs = [(label, pool.submit(arena_replay.run, rid, serial)) for rid, serial, label in DEVICES]
        for label, future in jobs:
            try:
                future.result()
            except Exception as error:
                errors[label] = str(error)
    assert all(sha(Path(p)) == digest for p, digest in inputs.items()), 'Capture changed during replay'
    if errors:
        raise RuntimeError('Trig replay failures: '+json.dumps(errors))
    print(json.dumps({'passed':True, 'devices':len(DEVICES), 'inputSha256':inputs}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['capture', 'replay', 'run'])
    args = parser.parse_args()
    if args.action in ('capture', 'run'):
        capture()
    if args.action in ('replay', 'run'):
        replay(build=args.action == 'run')
