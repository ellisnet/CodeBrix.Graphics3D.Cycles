# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Actual Cycles CPU traversal versus manually adapted WGSL traversal.

The fixture BVH builder is ours, using Cycles BVH2 packing; this is not a test
of the production Cycles scene manager or BVH builder.
"""
import array
import ctypes
import hashlib
import json
import random
import re
import struct
import subprocess

def extract(source, name):
    # Extraction only: function bodies are preserved verbatim, including macros.
    match = re.search(r'ccl_device\w*\s+[^\n]*\b' + name + r'\(', source)
    if not match: raise ValueError(name)
    start = source.index('{', match.start()); depth = 0
    for i in range(start, len(source)):
        if source[i] == '{': depth += 1
        if source[i] == '}':
            depth -= 1
            if depth == 0: return source[match.start():i+1]
    raise ValueError('Unclosed function ' + name)

def prepare(root, work, upstream, translated):
    extra = []
    for path, names in [('src/kernel/geom/object.h', ['bvh_clamp_direction','bvh_inverse_direction']),
                        ('src/kernel/bvh/nodes.h', ['bvh_aligned_node_intersect'])]:
        source = (upstream/path).read_text()
        extra += [source[:source.index('*/')+2]]
        extra += [extract(source,name) for name in names]
    (work/'bvh_extracted.h').write_text('\n\n'.join(extra))
    subprocess.run(['clang++','-std=c++20','-O2','-fPIC','-shared','-ffp-contract=off',
                    '-I',str(upstream/'src'),'-I',str(work),str(root/'native/bvh_reference.cpp'),
                    '-o',str(work/'libbvh_reference.so')],check=True)
    rng = random.Random(425172)
    triangles = []
    # Separated nondegenerate triangles, spread through three dimensions.
    for z in range(4):
        for y in range(8):
            for x in range(8):
                cx,cy,cz = x-3.5,y-3.5,z*1.5
                triangles.append([(cx-.38,cy-.34,cz),(cx+.4,cy-.3,cz+.08),(cx,cy+.4,cz-.05)])
    rows = [bytes(16)]; leaves = []; order = []; maximum_depth = 0
    def packf(v): return struct.pack('<4f',*v)
    def bounds(ids):
        pts = [p for i in ids for p in triangles[i]]
        return ([min(p[j] for p in pts)-1e-5 for j in range(3)],
                [max(p[j] for p in pts)+1e-5 for j in range(3)])
    def build(ids, depth):
        nonlocal maximum_depth
        maximum_depth = max(maximum_depth,depth)
        if len(ids)<=2:
            leaf = len(leaves); start = len(order); order.extend(ids)
            leaves.append(struct.pack('<4i',start,len(order),0,1))
            return -leaf-1
        lo,hi = bounds(ids); axis = max(range(3),key=lambda j:hi[j]-lo[j])
        ids.sort(key=lambda i:sum(p[axis] for p in triangles[i]))
        left,right = ids[:len(ids)//2],ids[len(ids)//2:]
        at = len(rows); rows.extend([bytes(16)]*4)
        child0,child1 = build(left,depth+1),build(right,depth+1)
        l0,h0 = bounds(left); l1,h1 = bounds(right)
        rows[at] = struct.pack('<4i',0,0,child0,child1)
        for j in range(3): rows[at+j+1] = packf([l0[j],l1[j],h0[j],h1[j]])
        return at
    assert build(list(range(len(triangles))),0)==1
    leaf_base = len(rows); rows += leaves
    prim_base = len(rows); rows += [packf([i,0,0,0]) for i in order]
    vert_base = len(rows); rows += [packf([*v,0]) for tri in triangles for v in tri]
    rows[0] = packf([len(triangles),leaf_base,prim_base,vert_base])
    scene = b''.join(rows); (work/'bvh.scene.bin').write_bytes(scene)
    rays = []
    for i in range(16384):
        origin = [rng.uniform(-7,7),rng.uniform(-7,7),rng.uniform(-4,9)]
        if i%3==0:
            tri = triangles[i%len(triangles)]
            dest = [sum(p[j] for p in tri)/3 for j in range(3)]
            direction = [dest[j]-origin[j] for j in range(3)]
        else: direction = [rng.uniform(-1,1) for _ in range(3)]
        if i<64: # Zero components and signed zero stress finite reciprocal handling.
            origin = [i%8-3.5,i//8-3.5,10]
            direction = [-0.0 if i%2 else 0.0,0.0,-1.0]
        rays += origin+[0]+direction+[10000]
    packed_rays = array.array('f',rays)
    lib = ctypes.CDLL(str(work/'libbvh_reference.so'))
    fp = ctypes.POINTER(ctypes.c_float)
    lib.cycles_bvh.argtypes = [fp,fp,fp,ctypes.c_int]
    scene_c = (ctypes.c_float*(len(scene)//4)).from_buffer_copy(scene)
    ray_c = (ctypes.c_float*len(rays)).from_buffer_copy(packed_rays)
    out = (ctypes.c_float*(16384*4))()
    lib.cycles_bvh(scene_c,ray_c,out,16384)
    (work/'bvh.input.bin').write_bytes(packed_rays.tobytes())
    (work/'bvh.output.bin').write_bytes(bytes(len(out)*4))
    (work/'bvh.expected.bin').write_bytes(bytes(out))
    (work/'bvh.wgsl').write_text(translated+'\n'+(root/'shaders/bvh.wgsl').read_text())
    (work/'bvh-fixture.json').write_text(json.dumps({'triangles':len(triangles),'rays':16384,
        'treeDepth':maximum_depth,'leaves':len(leaves),'traversalStackCapacity':192,
        'sceneBytes':len(scene),'sceneSha256':hashlib.sha256(scene).hexdigest(),
        'reference':'Original upstream traversal.h; extracted original aligned-node and direction helpers',
        'gpu':'Manual static-triangle traversal port; automatically translated triangle math',
        'limitations':['No instances','No motion','No visibility masks','No self-hit rejection',
                       'Fixture builder, not the production Cycles BVH builder']},indent=2)+'\n')
    return {'Name':'bvh','Shader':'bvh.wgsl','Buffers':['bvh.input.bin','bvh.output.bin','bvh.scene.bin'],
            'Output':1,'Dispatches':[{'Entry':'main','Groups':256}]}
