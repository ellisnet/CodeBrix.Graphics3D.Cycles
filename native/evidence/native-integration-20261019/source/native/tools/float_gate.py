#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Exact binary32 checks for the normal-math adapters on shipped GPU runtimes."""
import argparse
import concurrent.futures
import json
import math
import random
import struct
from pathlib import Path
from build import ROOT, WORK
from device_gate import sha, save
import arena_replay
from render_gate import DEVICES

OUT = WORK/'float-rounding'


def capture():
    out = OUT/'capture'
    out.mkdir(parents=True, exist_ok=True)
    def value(bits):
        return struct.unpack('<f', struct.pack('<I', bits))[0]
    pairs = [(0., 0.), (-0., 0.), (0., -0.), (-0., -0.), (1., -1.), (-1., 1.)]
    rng = random.Random(20261019)
    for i in range(32768):
        exponent = rng.randrange(30, 210)
        a = value((rng.getrandbits(1) << 31) | (exponent << 23) | rng.getrandbits(23))
        if i % 4 == 0:
            b = -a
        elif i % 4 == 1:
            b = value((rng.getrandbits(1) << 31) | ((exponent-24) << 23))
        else:
            other = max(1, min(254, exponent+rng.randrange(-24, 25)))
            b = value((rng.getrandbits(1) << 31) | (other << 23) | rng.getrandbits(23))
        pairs.append((a, b))
    # Reciprocals around the unit binade expose double rounding at midpoints.
    pairs += [(1., value(0x3f800000+i)) for i in range(-1024, 1025)]
    # Gradual underflow, the normal/subnormal boundary, and exact cancellation.
    for bits in (1, 2, 3, 0x3fffff, 0x7ffffe, 0x7fffff, 0x800000, 0x800001):
        x = value(bits)
        pairs += [(x, 1.), (-x, 1.), (x, 2.), (x, -x), (x, x)]
    count = len(pairs)
    output = 256+count*8
    arena = bytearray(output+count*12+256)
    reference = bytearray()
    for i, (a, b) in enumerate(pairs):
        struct.pack_into('<2f', arena, 256+i*8, a, b)
        # Binary64 has enough precision for these finite binary32 operands;
        # the final pack rounds once to binary32. No WGSL algorithm is mirrored.
        reference += struct.pack('<3f', a+b, a/(b if b else 1.), math.sqrt(abs(a)))
    (out/'run.arena.bin').write_bytes(arena)
    (out/'run.reference.bin').write_bytes(reference)
    params = [0]*32
    params[1], params[4] = count, output
    (out/'run.params.bin').write_bytes(struct.pack('<32I', *params))
    save(out/'run.capture.json', {
        'entry':'float_probe', 'recordSize':12, 'count':count,
        'outputOffset':output, 'errorOffset':0,
        'fields':[{'name':name, 'offset':4*i, 'size':4, 'floating':False}
                  for i, name in enumerate(('add_bits', 'divide_bits', 'sqrt_bits'))]})
    source = ROOT/'shaders/wavefront_float.wgsl'
    shader = '''// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
@group(0) @binding(0) var<storage,read_write> heap:array<u32>;
@group(0) @binding(1) var<uniform> params:array<vec4<u32>,8>;
fn p(i:u32)->u32{return params[i/4u][i%4u];}
''' + source.read_text() + '''
@compute @workgroup_size(64) fn float_probe(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=p(1u)){return;}
    let ab=heap[64u+2u*i];let bb=heap[65u+2u*i];
    let a=bitcast<f32>(ab);let b=bitcast<f32>(bb);
    let denominator=bitcast<f32>(select(bb,0x3f800000u,(bb&0x7fffffffu)==0u));
    let offset=p(4u)/4u+3u*i;
    heap[offset]=bitcast<u32>(rounded_add(a,b));
    heap[offset+1u]=bitcast<u32>(rounded_divide(a,denominator));
    heap[offset+2u]=bitcast<u32>(rounded_sqrt(bitcast<f32>(ab&0x7fffffffu)));
}
'''
    (out/'run.wgsl').write_text(shader)
    save(out/'generation.json', {'pairs':count, 'exactChecksPerDevice':count*3,
                                'shaderSha256':sha(source), 'generatorSha256':sha(Path(__file__)),
                                'scope':'Finite binary32 scalar arithmetic; not renderer parity.'})


def replay(build):
    arena_replay.configure(OUT/'capture', OUT/'replay', '/data/local/tmp/codebrix-float-rounding')
    if build:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            for future in [pool.submit(arena_replay.build, rid) for rid in sorted({d[0] for d in DEVICES})]:
                future.result()
    inputs = {str(p):sha(p) for p in (OUT/'capture').iterdir() if p.is_file()}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(arena_replay.run, rid, serial) for rid, serial, _ in DEVICES]:
            future.result()
    assert all(sha(Path(p)) == digest for p, digest in inputs.items()), 'Capture changed during replay'
    print(json.dumps({'passed':True, 'devices':len(DEVICES), 'inputSha256':inputs}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['capture', 'replay', 'run'])
    args = parser.parse_args()
    if args.action in ('capture', 'run'):
        capture()
    if args.action in ('replay', 'run'):
        replay(build=args.action == 'run')
