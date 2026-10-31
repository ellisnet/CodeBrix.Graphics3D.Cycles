#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the surface node oracle scenes (scenes/surface-nodes/) byte for byte.

The Bevel node (rounded box edges and a smooth sphere), the Raycast node (hit
distance and hit normal along the shading normal, and an only_local ray into a
box) and the Radial Tiling node (regular and irregular, rounded polygons), through
the public C API, lit by a sphere lamp with MIS and a constant world.
Usage: python3 native/oracle/scenes/author_surface_nodes.py [--check]."""
import argparse
import json
from pathlib import Path
from author_bsdf import single, shader, quad, sphere, BACK, FLOOR, f32

HERE = Path(__file__).resolve().parent/'surface-nodes'


def box(handle, material, center, size):
    positions = [f32(center[k]+(size[k]*0.5 if i >> k & 1 else -size[k]*0.5)) for i in range(8) for k in range(3)]
    indices = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5]
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices}, 'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


def scene(scene_id, description, materials, shapes):
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                 'min_bounce': 2, 'max_bounce': 4, 'filter_glossy': 0.0, 'caustics_reflective': True, 'caustics_refractive': True}},
             {'handle': 'film', 'scene': 'film', 'set': {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             single('wall', 'diffuse_bsdf', {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}),
             single('floor-material', 'diffuse_bsdf', {'color': [0.5, 0.45, 0.35], 'roughness': 0.0}),
             *materials, *quad('back', 'wall', BACK), *quad('floor', 'floor-material', FLOOR), *shapes,
             shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                    [['b', 'Background', 'output', 'Surface']]),
             {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
             shader('emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
             {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['emitter']}},
             {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description,
            'sourceFixture': 'native/cycles/raytrace_nodes_fixture.h and bsdf_fixture.h (the same nodes)',
            'render': {'width': 32, 'height': 24, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


def bevel_material(handle, radius, samples):
    return shader(handle, [{'handle': 'bevel', 'type': 'bevel', 'set': {'radius': radius, 'samples': samples}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.5, 0.35], 'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.3, 0.4, 0.8], 'roughness': 0.3}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.4}}],
                  [['bevel', 'Normal', 'd', 'Normal'], ['bevel', 'Normal', 'g', 'Normal'], ['g', 'BSDF', 'm', 'Closure1'],
                   ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def raycast_material(handle, local):
    nodes = [{'handle': 'ray', 'type': 'raycast', 'set': {'length': 1.6 if local else 2.5, 'only_local': local}},
             {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.6, 0.5, 0.35], 'roughness': 0.2}},
             {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3}},
             {'handle': 'm', 'type': 'mix_closure', 'set': {}},
             {'handle': 't', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.3}},
             {'handle': 'c', 'type': 'combine_xyz', 'set': {}}]
    connect = [['ray', 'Is Hit', 'm', 'Fac'], ['ray', 'Hit Distance', 't', 'Value1'], ['t', 'Value', 'c', 'X'], ['t', 'Value', 'c', 'Z'],
               ['c', 'Vector', 'd', 'Color'], ['ray', 'Hit Normal', 'g', 'Color'], ['g', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'],
               ['m', 'Closure', 'output', 'Surface']]
    if local:
        nodes.append({'handle': 'dir', 'type': 'color', 'set': {'value': [0.15, -1.0, 0.35]}})
        connect.append(['dir', 'Color', 'ray', 'Direction'])
    return shader(handle, nodes, connect)


def radial_material(handle, sides, roundness, normalize):
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}},
                           {'handle': 'rt', 'type': 'radial_tiling', 'set': {'r_gon_sides': sides, 'r_gon_roundness': roundness, 'use_normalize': normalize}},
                           {'handle': 'c', 'type': 'combine_xyz', 'set': {}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.3}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.4}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.3}}],
                  [['geo', 'Position', 'rt', 'Vector'], ['rt', 'Segment Coordinates', 'd', 'Color'], ['rt', 'Segment ID', 'c', 'X'],
                   ['rt', 'Segment Width', 'c', 'Y'], ['rt', 'Segment Rotation', 'c', 'Z'], ['c', 'Vector', 'g', 'Color'],
                   ['g', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def scenes():
    out = {}
    out['bevel.json'] = scene('surface-nodes/bevel', 'Bevel node: rounded edges of a box and a smooth sphere, the bevel normal shading a diffuse/glossy mix',
                              [bevel_material('bevel-box', 0.3, 8), bevel_material('bevel-sphere', 0.2, 4)],
                              box('box', 'bevel-box', (-1.4, -1.0, 0.5), (1.6, 1.8, 1.4))+sphere('sphere', 'bevel-sphere', (1.6, -0.9, 0.4), 1.0))
    out['raycast.json'] = scene('surface-nodes/raycast', 'Raycast node: hit distance and hit normal along the shading normal, and an only_local ray into a box',
                                [raycast_material('ray-sphere', False), raycast_material('ray-box', True)],
                                sphere('sphere', 'ray-sphere', (1.6, -0.9, 0.4), 1.0)+box('box', 'ray-box', (-1.4, -1.0, 0.5), (1.6, 1.8, 1.4)))
    out['radial-tiling.json'] = scene('surface-nodes/radial-tiling', 'Radial Tiling node: a regular rounded pentagon (normalized) and an irregular 3.5-gon',
                                      [radial_material('rt-regular', 5.0, 0.3, True), radial_material('rt-irregular', 3.5, 0.6, False)],
                                      quad('panel-a', 'rt-regular', [(-3.6, -2, 1), (-0.2, -2, 1), (-0.2, 1.5, 1), (-3.6, 1.5, 1)])+
                                      quad('panel-b', 'rt-irregular', [(0.2, -2, 1), (3.6, -2, 1), (3.6, 1.5, 1), (0.2, 1.5, 1)]))
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
