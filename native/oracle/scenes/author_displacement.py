#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the displacement oracle scenes (scenes/displacement/) byte for byte.

Displacement without subdivision: the host displaces the mesh vertices before the BVH build
through DEVICE_KERNEL_SHADER_EVAL_DISPLACE (displacement method True and Both), and the bump
from displacement (methods Bump and Both, the latter at the undisplaced position) runs in the
surface programs. Displacement and Vector Displacement nodes in object, world and tangent space,
procedural heights, smooth and flat grids, an object transform. Usage:
python3 native/oracle/scenes/author_displacement.py [--check]."""
import argparse
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent/'displacement'


def f32(v):
    return round(struct.unpack('<f', struct.pack('<f', v))[0], 7)


def grid(handle, material, size, n, smooth, height=-1.6):
    """An n x n quad grid in the y = height plane, centred, two triangles per quad."""
    positions, indices = [], []
    for j in range(n+1):
        for i in range(n+1):
            positions += [f32(size*(i/n-0.5)), height, f32(size*(j/n-0.5))]
    for j in range(n):
        for i in range(n):
            a = j*(n+1)+i
            indices += [a, a+n+1, a+1, a+1, a+n+1, a+n+2]
    return {'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices},
            'set': {'used_shaders': [material], 'smooth': [smooth]*(n*n*2)}}


def rotate_y(angle, scale, offset):
    c, s = math.cos(angle), math.sin(angle)
    rows = [[c*scale[0], 0.0, s*scale[2], offset[0]], [0.0, scale[1], 0.0, offset[1]], [-s*scale[0], 0.0, c*scale[2], offset[2]]]
    return [f32(v) for row in rows for v in row]


def material(handle, method, height_nodes, height_connect, color):
    """A diffuse/glossy mix whose Displacement output comes from height_nodes."""
    nodes = [{'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': color, 'roughness': 0.2}},
             {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.6, 0.6, 0.65], 'roughness': 0.3}},
             {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.3}}]+height_nodes
    connect = [['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']]+height_connect
    return {'handle': handle, 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect},
            'set': {'emission_sampling_method': 'none', 'displacement_method': method}}


def height(space, scale, midlevel=0.5):
    """Noise height through a Displacement node."""
    return ([{'handle': 'noise', 'type': 'noise_texture', 'set': {'scale': 2.5, 'detail': 2.0}},
             {'handle': 'disp', 'type': 'displacement', 'set': {'space': space, 'scale': scale, 'midlevel': midlevel}}],
            [['noise', 'Fac', 'disp', 'Height'], ['disp', 'Displacement', 'output', 'Displacement']])


def vector(space, scale):
    """Wave colour through a Vector Displacement node."""
    return ([{'handle': 'wave', 'type': 'wave_texture', 'set': {'scale': 1.7, 'distortion': 1.5}},
             {'handle': 'vdisp', 'type': 'vector_displacement', 'set': {'space': space, 'scale': scale, 'midlevel': 0.4}}],
            [['wave', 'Color', 'vdisp', 'Vector'], ['vdisp', 'Displacement', 'output', 'Displacement']])


def scene(identifier, description, materials, objects):
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                 'min_bounce': 2, 'max_bounce': 3, 'filter_glossy': 0.0}},
             {'handle': 'film', 'scene': 'film', 'set': {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 0.96, -0.28, 0.0, 0.0, 0.28, 0.96, -7.0]}},
             *materials, *objects,
             {'handle': 'world', 'create': 'shader', 'graph': {'nodes': [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                                                                'connect': [['b', 'Background', 'output', 'Surface']]}, 'set': {'emission_sampling_method': 'none'}},
             {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
             {'handle': 'emitter', 'create': 'shader', 'graph': {'nodes': [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}],
                                                                  'connect': [['e', 'Emission', 'output', 'Surface']]}, 'set': {'emission_sampling_method': 'none'}},
             {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.3, 'is_sphere': True, 'use_mis': True, 'strength': [120, 100, 80], 'used_shaders': ['emitter']}},
             {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 1.8, 0.0, 1.0, 0.0, 2.2, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': 'displacement/'+identifier, 'description': description,
            'sourceFixture': 'native/oracle/scenes/author_displacement.py',
            'render': {'width': 32, 'height': 24, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


def scenes():
    out = {}
    # True displacement: object space under a rotated, scaled object, and world space.
    nodes, connect = height('object', 0.45)
    a = material('object-height', 'true', nodes, connect, [0.6, 0.45, 0.3])
    nodes, connect = height('world', 0.35, 0.4)
    b = material('world-height', 'true', nodes, connect, [0.35, 0.5, 0.6])
    out['true-height.json'] = scene('true-height', 'True displacement (displacement method True) of smooth grids by a noise height '
                                    'through Displacement nodes in object space (rotated and scaled object) and world space',
                                    [a, b],
                                    [grid('left', 'object-height', 2.6, 20, True, 0.0),
                                     {'handle': 'left-object', 'create': 'object', 'set': {'geometry': 'left', 'tfm': rotate_y(0.5, (1.1, 0.8, 0.9), (-1.5, -1.4, 0.4))}},
                                     grid('right', 'world-height', 2.6, 20, True),
                                     {'handle': 'right-object', 'create': 'object', 'set': {'geometry': 'right', 'tfm': [1, 0, 0, 1.5, 0, 1, 0, 0, 0, 0, 1, 0.4]}}])
    # Vector displacement: object, world and tangent space (no UV map: the tangent is dPdu).
    mats, objs = [], []
    for k, space in enumerate(['object', 'world', 'tangent']):
        nodes, connect = vector(space, 0.3)
        mats.append(material('vector-'+space, 'true', nodes, connect, [0.4+0.15*k, 0.5, 0.45]))
        objs += [grid('grid-'+space, 'vector-'+space, 1.9, 16, k != 1, 0.0),
                 {'handle': 'grid-'+space+'-object', 'create': 'object', 'set': {'geometry': 'grid-'+space,
                  'tfm': rotate_y(0.3*(k-1), (1.0, 1.0, 1.0), (-2.1+2.1*k, -1.5, 0.3))}}]
    out['true-vector.json'] = scene('true-vector', 'True displacement by a wave colour through Vector Displacement nodes in object, '
                                    'world and tangent space (the tangent of a mesh without UV map is dPdu); the world-space grid is flat shaded',
                                    mats, objs)
    # Both: displaced vertices and the bump from displacement at the undisplaced position and normal.
    nodes, connect = height('object', 0.4)
    a = material('both-smooth', 'both', nodes, connect, [0.6, 0.5, 0.35])
    nodes, connect = height('world', 0.3, 0.45)
    b = material('both-flat', 'both', nodes, connect, [0.4, 0.55, 0.5])
    out['both.json'] = scene('both', 'Displacement method Both: true displacement plus the bump from displacement at the undisplaced position '
                             '(ATTR_STD_POSITION_UNDISPLACED) and normal, on a smooth grid (undisplaced vertex normals) and a flat grid '
                             '(undisplaced face normals)',
                             [a, b],
                             [grid('smooth', 'both-smooth', 2.6, 18, True, 0.0),
                              {'handle': 'smooth-object', 'create': 'object', 'set': {'geometry': 'smooth', 'tfm': rotate_y(-0.4, (1.0, 0.9, 1.0), (-1.5, -1.4, 0.4))}},
                              grid('flat', 'both-flat', 2.6, 18, False),
                              {'handle': 'flat-object', 'create': 'object', 'set': {'geometry': 'flat', 'tfm': [1, 0, 0, 1.5, 0, 1, 0, 0, 0, 0, 1, 0.4]}}])
    # Bump only: the Displacement node in the bump program (no true displacement).
    nodes, connect = height('object', 0.5)
    a = material('bump-object', 'bump', nodes, connect, [0.55, 0.45, 0.35])
    nodes, connect = vector('world', 0.3)
    b = material('bump-vector', 'bump', nodes, connect, [0.35, 0.45, 0.6])
    out['bump.json'] = scene('bump', 'Displacement method Bump with Displacement and Vector Displacement nodes: the bump from displacement '
                             'in the surface program, no true displacement',
                             [a, b],
                             [grid('a', 'bump-object', 2.6, 6, True, 0.0),
                              {'handle': 'a-object', 'create': 'object', 'set': {'geometry': 'a', 'tfm': rotate_y(0.4, (1.0, 1.0, 1.0), (-1.5, -1.4, 0.4))}},
                              grid('b', 'bump-vector', 2.6, 6, True),
                              {'handle': 'b-object', 'create': 'object', 'set': {'geometry': 'b', 'tfm': [1, 0, 0, 1.5, 0, 1, 0, 0, 0, 0, 1, 0.4]}}])
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
                raise SystemExit('Scene differs: '+name)
        else:
            path.write_text(text)
    print('checked' if args.check else 'written', len(scenes()), 'scenes')


if __name__ == '__main__':
    main()
