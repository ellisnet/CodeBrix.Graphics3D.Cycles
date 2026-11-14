#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the bake scenes of a deforming target and of zero-area triangles (scenes/bake/) byte for byte.

The sampling family room of author_bake.py: the UV-mapped sphere baked under motion blur with deformation motion (three
position steps; the kernel places every texel on the static positions, while the rays that leave it meet the sphere's
motion triangles at the ray's time), and the sphere with its zero-area pole triangles kept in the UV layout, baked
with the smooth Normal type (the interpolated normal is defined there) and with the shading bake types that are finite
there (bake/zero-area-<type>; the flat Normal type and the direct-light types fail with "Nonfinite bake output" on every
backend, so they have no scene).
Usage: python3 native/oracle/scenes/author_bake_motion.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_bake as bake
import author_sampling as room

HERE = Path(__file__).resolve().parent/'bake'
f32 = room.f32


def deforming():
    document = bake.bake_scene('bake/deformation-motion', 'Combined of a sphere with deformation motion under motion blur (texels on the static positions)',
                               {'type': 'combined'}, integrator={'motion_blur': True})
    document['sourceFixture'] = 'native/oracle/scenes/author_bake_motion.py (bakes in the sampling family room)'
    for n in document['nodes']:
        if n['handle'] == 'ball':
            p = n['mesh']['positions']
            shift = lambda s: [f32(v+(0.3*s if k % 3 == 0 else 0.1*s if k % 3 == 1 else 0.0)) for k, v in enumerate(p)]
            # The centre step is the mesh's own positions, exactly (cbc_host_motion_positions requires it).
            steps = shift(-1)+list(p)+shift(1)
            n['mesh']['motion'] = {'steps': 3, 'positions': steps}
        if n['handle'] == 'camera':
            n['set']['shuttertime'] = 0.5
    return document


def pole_sphere(handle, material, center, radius, stacks=8, slices=12):
    """bake.uv_sphere with the pole triangles kept: each has no area in 3D (two corners at the pole) but area in UV."""
    positions, indices, uv = [], [], []
    for i in range(stacks+1):
        for j in range(slices+1):
            th, ph = math.pi*i/stacks, 2*math.pi*j/slices
            p = (math.sin(th)*math.cos(ph), math.cos(th), math.sin(th)*math.sin(ph))
            positions += [f32(center[k]+radius*p[k]) for k in range(3)]
    for i in range(stacks):
        for j in range(slices):
            a = i*(slices+1)+j
            b = a+slices+1
            corner = lambda r, c: [f32((c+0.02)/(slices+0.04)), f32(1.0-(r+0.02)/(stacks+0.04))]
            indices += [a, b, a+1, a+1, b, b+1]
            uv += corner(i, j)+corner(i+1, j)+corner(i, j+1)+corner(i, j+1)+corner(i+1, j)+corner(i+1, j+1)
    faces = len(indices)//3
    return {'handle': handle, 'create': 'mesh',
            'mesh': {'positions': positions, 'indices': indices,
                     'attributes': [{'name': 'uv', 'value': 'point2', 'domain': 'corner', 'flags': ['standard'], 'data': uv}]},
            'set': {'used_shaders': [material], 'smooth': [True]*faces}}


def zero_area_normal():
    document = bake.bake_scene('bake/zero-area-smooth-normal', 'The smooth Normal bake of a sphere whose zero-area pole triangles are in the UV layout', {'type': 'normal'})
    document['sourceFixture'] = 'native/oracle/scenes/author_bake_motion.py (bakes in the sampling family room)'
    document['nodes'] = [pole_sphere('ball', 'textured', (0.0, -0.9, 0.4), 1.0) if n['handle'] == 'ball' else n for n in document['nodes']]
    return document


# The diffuse, diffuse_direct, glossy, glossy_direct, transmission and transmission_direct bakes of this sphere fail on every
# backend with "Nonfinite bake output" (the direct light at a zero-area texel), so they have no scene.
ZERO_AREA_TYPES = [('combined', None), ('emission', None), ('ao', None), ('diffuse_indirect', None), ('diffuse_color', None),
                   ('glossy_indirect', None), ('glossy_color', None), ('transmission_indirect', None), ('transmission_color', None),
                   ('roughness', None), ('uv', None), ('object_id', None), ('material_id', None), ('depth', None), ('mist', None),
                   ('aov_color', 'Tint'), ('aov_value', 'Mask')]


def zero_area(kind, aov):
    """The kind bake of the pole sphere (zero-area pole triangles in the UV layout)."""
    request = {'type': kind, **({'aov': aov} if aov else {})}
    document = bake.bake_scene('bake/zero-area-'+kind.replace('_', '-'), 'The '+kind+' bake of a sphere whose zero-area pole triangles are in the UV layout', request)
    document['sourceFixture'] = 'native/oracle/scenes/author_bake_motion.py (bakes in the sampling family room)'
    document['nodes'] = [pole_sphere('ball', 'textured', (0.0, -0.9, 0.4), 1.0) if n['handle'] == 'ball' else n for n in document['nodes']]
    return document


def scenes():
    out = {'deformation-motion.json': deforming(), 'zero-area-smooth-normal.json': zero_area_normal()}
    for kind, aov in ZERO_AREA_TYPES:
        out['zero-area-'+kind.replace('_', '-')+'.json'] = zero_area(kind, aov)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    for name, document in scenes().items():
        text = json.dumps(document, indent=1)+'\n'
        path = HERE/name
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
