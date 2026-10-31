#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the BSDF family oracle scenes (scenes/bsdf/) byte for byte.

Smooth spheres with the Metallic, Beckmann (glossy, anisotropic, refraction,
glass, metallic), Ashikhmin-Shirley, Ashikhmin velvet and Toon BSDFs on a
diffuse floor and wall, lit by a sphere lamp with MIS (BSDF evaluation through
next-event estimation) and a constant world, through the public C API.
Usage: python3 native/oracle/scenes/author_bsdf.py [--check]."""
import argparse
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent/'bsdf'


def f32(v):
    return round(struct.unpack('<f', struct.pack('<f', v))[0], 7)


def shader(handle, nodes, connect):
    return {'handle': handle, 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect}, 'set': {'emission_sampling_method': 'none'}}


def single(handle, node_type, values, output='BSDF'):
    return shader(handle, [{'handle': 'n', 'type': node_type, 'set': values}], [['n', output, 'output', 'Surface']])


def mixed(handle, first, second, fac):
    """Mix of two (type, values) nodes."""
    return shader(handle, [{'handle': 'a', 'type': first[0], 'set': first[1]}, {'handle': 'b', 'type': second[0], 'set': second[1]},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': fac}}],
                  [['a', 'BSDF', 'm', 'Closure1'], ['b', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def tangent_shader(handle, node_type, values):
    """An anisotropic node with a constant tangent (as the native fixture)."""
    return shader(handle, [{'handle': 'n', 'type': node_type, 'set': values}, {'handle': 't', 'type': 'color', 'set': {'value': [0.8, 0.3, 0.2]}}],
                  [['t', 'Color', 'n', 'Tangent'], ['n', 'BSDF', 'output', 'Surface']])


def sphere(handle, material, center, radius, stacks=10, slices=14):
    positions, indices = [], []
    for i in range(stacks+1):
        for j in range(slices+1):
            th, ph = math.pi*i/stacks, 2*math.pi*j/slices
            p = (math.sin(th)*math.cos(ph), math.cos(th), math.sin(th)*math.sin(ph))
            positions += [f32(center[k]+radius*p[k]) for k in range(3)]
    for i in range(stacks):
        for j in range(slices):
            a = i*(slices+1)+j
            b = a+slices+1
            indices += [a, b, a+1, a+1, b, b+1]
    faces = len(indices)//3
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices},
             'set': {'used_shaders': [material], 'smooth': [True]*faces}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


def quad(handle, material, corners):
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': [c for p in corners for c in p], 'indices': [0, 1, 2, 0, 2, 3]},
             'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


BACK = [(-4, -3, 3), (4, -3, 3), (4, 3, 3), (-4, 3, 3)]
FLOOR = [(-4, -2, -2), (4, -2, -2), (4, -2, 3), (-4, -2, 3)]


def scene(scene_id, description, materials, filter_glossy=0.0):
    """Three spheres (left to right) with materials[0..2]."""
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                 'min_bounce': 2, 'max_bounce': 4, 'filter_glossy': filter_glossy, 'caustics_reflective': True, 'caustics_refractive': True}},
             {'handle': 'film', 'scene': 'film', 'set': {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             single('wall', 'diffuse_bsdf', {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}),
             single('floor-material', 'diffuse_bsdf', {'color': [0.5, 0.45, 0.35], 'roughness': 0.0}),
             *materials,
             *quad('back', 'wall', BACK), *quad('floor', 'floor-material', FLOOR)]
    for k, m in enumerate(materials):
        nodes += sphere('sphere-'+str(k), m['handle'], (-2.3+2.3*k, -0.9, 0.4), 1.0)
    nodes += [shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                     [['b', 'Background', 'output', 'Surface']]),
              {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
              shader('emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
              {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['emitter']}},
              {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description, 'sourceFixture': 'native/cycles/bsdf_fixture.h (the same closure families)',
            'render': {'width': 32, 'height': 24, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


def metallic(fresnel, distribution, roughness, **extra):
    values = {'fresnel_type': fresnel, 'distribution': distribution, 'roughness': roughness, 'color': [0.9, 0.62, 0.31],
              'edge_tint': [0.95, 0.86, 0.7], 'ior': [0.18, 0.42, 1.37], 'k': [3.42, 2.35, 1.77]}
    values.update(extra)
    return values


def scenes():
    out = {}
    out['metallic.json'] = scene('bsdf/metallic', 'Metallic BSDF: F82 tint (GGX), F82 tint with thin film (multiscatter GGX), physical conductor anisotropic (multiscatter GGX)', [
        single('m-f82', 'metallic_bsdf', metallic('f82', 'ggx', 0.35)),
        single('m-film', 'metallic_bsdf', metallic('f82', 'multi_ggx', 0.3, thin_film_thickness=320.0, thin_film_ior=1.45)),
        tangent_shader('m-physical', 'metallic_bsdf', metallic('physical_conductor', 'multi_ggx', 0.45, anisotropy=0.6, rotation=0.2))])
    out['beckmann.json'] = scene('bsdf/beckmann', 'Beckmann distribution: anisotropic glossy, glass, and a physical-conductor Metallic BSDF mixed with rough refraction', [
        tangent_shader('b-glossy', 'glossy_bsdf', {'distribution': 'beckmann', 'roughness': 0.4, 'anisotropy': -0.5, 'rotation': 0.15, 'color': [0.3, 0.6, 0.85]}),
        single('b-glass', 'glass_bsdf', {'distribution': 'beckmann', 'roughness': 0.3, 'IOR': 1.5, 'color': [0.8, 0.9, 0.95]}),
        mixed('b-mix', ('metallic_bsdf', metallic('physical_conductor', 'beckmann', 0.35)),
              ('refraction_bsdf', {'distribution': 'beckmann', 'roughness': 0.25, 'IOR': 1.33, 'color': [0.9, 0.95, 1.0]}), 0.5)])
    out['ashikhmin-shirley.json'] = scene('bsdf/ashikhmin-shirley', 'Ashikhmin-Shirley glossy: isotropic, anisotropic with a tangent, and mixed with diffuse', [
        single('as-iso', 'glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.3, 'color': [0.85, 0.7, 0.4]}),
        tangent_shader('as-aniso', 'glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.45, 'anisotropy': 0.6, 'rotation': 0.1, 'color': [0.4, 0.7, 0.85]}),
        mixed('as-mix', ('glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.2, 'color': [0.9, 0.9, 0.9]}),
              ('diffuse_bsdf', {'color': [0.7, 0.4, 0.2], 'roughness': 0.43}), 0.35)])
    out['velvet.json'] = scene('bsdf/velvet', 'Sheen BSDF with the Ashikhmin velvet distribution: two sigmas and a mix with diffuse', [
        single('v-low', 'sheen_bsdf', {'distribution': 'ashikhmin', 'roughness': 0.3, 'color': [0.8, 0.3, 0.5]}),
        single('v-high', 'sheen_bsdf', {'distribution': 'ashikhmin', 'roughness': 1.0, 'color': [0.4, 0.7, 0.3]}),
        mixed('v-mix', ('sheen_bsdf', {'distribution': 'ashikhmin', 'roughness': 0.6, 'color': [0.6, 0.5, 0.9]}),
              ('diffuse_bsdf', {'color': [0.7, 0.4, 0.2], 'roughness': 0.43}), 0.5)])
    out['toon.json'] = scene('bsdf/toon', 'Toon BSDF: diffuse, glossy, and the two components mixed', [
        single('t-diffuse', 'toon_bsdf', {'component': 'diffuse', 'size': 0.5, 'smooth': 0.1, 'color': [0.9, 0.5, 0.2]}),
        single('t-glossy', 'toon_bsdf', {'component': 'glossy', 'size': 0.3, 'smooth': 0.2, 'color': [0.6, 0.8, 0.9]}),
        mixed('t-mix', ('toon_bsdf', {'component': 'diffuse', 'size': 0.8, 'smooth': 0.0, 'color': [0.5, 0.6, 0.7]}),
              ('toon_bsdf', {'component': 'glossy', 'size': 0.1, 'smooth': 0.05, 'color': [0.9, 0.9, 0.8]}), 0.3)])
    out['glossy-filter.json'] = scene('bsdf/glossy-filter', 'Glossy filtering (filter_glossy 1): sharp GGX glossy, sharp Beckmann glass and sharp Ashikhmin-Shirley over a diffuse room', [
        single('f-ggx', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.05, 'color': [0.85, 0.85, 0.9]}),
        single('f-glass', 'glass_bsdf', {'distribution': 'beckmann', 'roughness': 0.05, 'IOR': 1.45, 'color': [0.9, 0.95, 0.95]}),
        mixed('f-mix', ('glossy_bsdf', {'distribution': 'ashikhmin_shirley', 'roughness': 0.1, 'color': [0.8, 0.7, 0.5]}),
              ('diffuse_bsdf', {'color': [0.4, 0.5, 0.6], 'roughness': 0.2}), 0.5)], filter_glossy=1.0)
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
