#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Contending, disjoint byte/halfword writes through the production state helper."""
import argparse
import concurrent.futures
import json
import random
import struct
from pathlib import Path
from build import ROOT, WORK
from device_gate import sha, save
from render_gate import DEVICES
import arena_replay

OUT = WORK/'packed-state'


def capture():
    out = OUT/'capture'
    out.mkdir(parents=True, exist_ok=True)
    count = 65537
    words = (count+3)//4
    offset = 256+4*count
    arena = bytearray(b'\xa5'*(offset+8*words+256))
    struct.pack_into('<I', arena, 0, 0)
    expected = bytearray(b'\xa5'*(8*words))
    rng = random.Random(20261019)
    for i in range(count):
        value = rng.getrandbits(32)
        struct.pack_into('<I', arena, 256+4*i, value)
        expected[8*(i//4)+i%4] = value&255
        if i < (count+1)//2:
            struct.pack_into('<H', expected, 8*(i//2)+4+2*(i%2), value&65535)
    (out/'run.arena.bin').write_bytes(arena)
    (out/'run.reference.bin').write_bytes(expected)
    params = [0]*32
    params[1], params[4] = count, offset
    (out/'run.params.bin').write_bytes(struct.pack('<32I', *params))
    save(out/'run.capture.json', {
        'entry':'packed_probe', 'recordSize':8, 'count':words,
        'outputOffset':offset, 'errorOffset':0,
        'fields':[{'name':name, 'offset':4*i, 'size':4, 'floating':False}
                  for i, name in enumerate(('bytes', 'halves'))]})
    source = ROOT/'shaders/controls.wgsl'
    shader = source.read_text() + '''
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
@compute @workgroup_size(64) fn packed_probe(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=p(1u)){return;}
    let value=load_word(256u+4u*i);let byte_address=p(4u)+8u*(i/4u)+i%4u;
    masked(byte_address,~value,8u);masked(byte_address,0u,8u);masked(byte_address,value,8u);
    if(((load_word(byte_address)>>(8u*(byte_address%4u)))&255u)!=(value&255u)){fail();}
    if(i<(p(1u)+1u)/2u){
        let half_address=p(4u)+8u*(i/2u)+4u+2u*(i%2u);
        masked(half_address,~value,16u);masked(half_address,0u,16u);masked(half_address,value,16u);
        if(load_half(half_address)!=(value&65535u)){fail();}
    }
}
'''
    (out/'run.wgsl').write_text(shader)
    save(out/'generation.json', {'invocations':count, 'outputWords':2*words,
                                'byteWriters':count, 'halfwordWriters':(count+1)//2,
                                'updatesPerField':3, 'shaderSha256':sha(source),
                                'generatorSha256':sha(Path(__file__)),
                                'scope':'Concurrent disjoint packed fields, including untouched tail fields; not renderer parity.'})


def replay(build):
    arena_replay.configure(OUT/'capture', OUT/'replay', '/data/local/tmp/codebrix-packed-state')
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
        raise RuntimeError('Packed-state replay failures: '+json.dumps(errors))
    print(json.dumps({'passed':True, 'devices':len(DEVICES), 'inputSha256':inputs}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['capture', 'replay', 'run'])
    args = parser.parse_args()
    if args.action in ('capture', 'run'):
        capture()
    if args.action in ('replay', 'run'):
        replay(build=args.action == 'run')
