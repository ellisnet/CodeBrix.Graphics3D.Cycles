#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the ambient occlusion oracle scenes (scenes/ao/) byte for byte.

The scenes re-express native/cycles/ao_render_gate.cpp cases through the public
C API: fast GI in its additive mode with a per-object AO distance, and the
Ambient Occlusion shader node (global, and only_local with inside on an
instanced box). Usage: python3 native/oracle/scenes/author_ao.py [--check]."""
import argparse
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent/'ao'


def graph(nodes, connect):
    return {'nodes': nodes, 'connect': connect}


def shader(handle, nodes, connect):
    return {'handle': handle, 'create': 'shader', 'graph': graph(nodes, connect), 'set': {'emission_sampling_method': 'none'}}


def diffuse(handle, color):
    return shader(handle, [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': color, 'roughness': 0.25}}],
                  [['d', 'BSDF', 'output', 'Surface']])


def mixed(handle):
    return shader(handle, [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.55, 0.32, 0.18], 'roughness': 0.31}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.2, 0.5, 0.8], 'roughness': 0.35}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.4}}],
                  [['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def ao_node(handle, local):
    ao = {'color': [0.9, 0.7, 0.4], 'distance': 0.9 if local else 1.4, 'samples': 6 if local else 4, 'only_local': local, 'inside': local}
    return shader(handle, [{'handle': 'ao', 'type': 'ambient_occlusion', 'set': ao},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.3, 0.4, 0.8], 'roughness': 0.3}},
                           {'handle': 'm', 'type': 'mix_closure'}],
                  [['ao', 'Color', 'd', 'Color'], ['ao', 'AO', 'm', 'Fac'], ['g', 'BSDF', 'm', 'Closure1'],
                   ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def quad(handle, material, corners, ao_distance=None):
    out = [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': [c for p in corners for c in p], 'indices': [0, 1, 2, 0, 2, 3]},
            'set': {'used_shaders': [material]}},
           {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]
    if ao_distance is not None:
        out[1]['set']['ao_distance'] = ao_distance
    return out


def box(handle, material):
    positions = [c for i in range(8) for c in (0.5 if i & 1 else -0.5, 0.5 if i & 2 else -0.5, 0.5 if i & 4 else -0.5)]
    indices = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5]
    return {'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices}, 'set': {'used_shaders': [material]}}


def instance(handle, geometry, x, angle, scale):
    # transform_translate(x,-1.3,-0.6) * transform_rotate(angle, y) * transform_scale(scale), row-major 3x4,
    # rounded through float32 as Cycles stores it.
    c, s = math.cos(angle), math.sin(angle)
    rows = [[c*scale[0], 0.0, s*scale[2], x], [0.0, scale[1], 0.0, -1.3], [-s*scale[0], 0.0, c*scale[2], -0.6]]
    import struct
    f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
    return {'handle': handle, 'create': 'object', 'set': {'geometry': geometry, 'tfm': [round(f32(v), 7) for row in rows for v in row]}}


def scene(scene_id, description, fixture, integrator, materials, objects):
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                 'min_bounce': 2, 'max_bounce': 4, 'filter_glossy': 0.0, **integrator}},
             {'handle': 'film', 'scene': 'film', 'set': {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             *materials, *objects,
             shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                    [['b', 'Background', 'output', 'Surface']]),
             {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
             shader('emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
             {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['emitter']}},
             {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description, 'sourceFixture': fixture,
            'render': {'width': 32, 'height': 24, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


BACK = [(-4, -3, 3), (4, -3, 3), (4, 3, 3), (-4, 3, 3)]
FLOOR = [(-4, -2, -2), (4, -2, -2), (4, -2, 3), (-4, -2, 3)]
CENTER = [(-0.8, -2, 0), (0.8, -2, 0), (0.8, 1.1, 0), (-0.8, 1.1, 0)]


def scenes():
    walls = [diffuse('wall', [0.6, 0.62, 0.65]), mixed('floor-material')]
    fast_gi = scene('ao/fast-gi-additive', 'fast GI ambient occlusion, additive mode, with AO bounce limit and per-object AO distances',
                    'native/cycles/ao_render_gate.cpp cases 1 and 2 combined (replace-object-distance distances, additive settings)',
                    {'ao_bounces': 1, 'ao_factor': 0.5, 'ao_distance': 1.2, 'ao_additive_factor': 0.7},
                    walls+[diffuse('center-material', [0.5, 0.45, 0.3])],
                    quad('back', 'wall', BACK)+quad('floor', 'floor-material', FLOOR, 1.5)+quad('center', 'center-material', CENTER, 0.35))
    node = scene('ao/ao-node', 'Ambient Occlusion shader node: hemisphere occlusion driving a BSDF mix, and only_local with inside on an instanced box',
                 'native/cycles/ao_render_gate.cpp cases 6 and 7 combined',
                 {'ao_bounces': 0, 'ao_factor': 0.0, 'ao_distance': 1.0, 'ao_additive_factor': 0.0},
                 walls+[ao_node('ao-global', False), ao_node('ao-local', True)],
                 quad('back', 'wall', BACK)+quad('floor', 'floor-material', FLOOR)+quad('center', 'ao-global', CENTER)+
                 [box('box', 'ao-local'), instance('box-0', 'box', -1.9, -0.4, (1.1, 1.4, 0.9)), instance('box-1', 'box', 1.9, 0.6, (1.1, 1.4, 0.9))])
    return {'fast-gi-additive.json': fast_gi, 'ao-node.json': node}


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
