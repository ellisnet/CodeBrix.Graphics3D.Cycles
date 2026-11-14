#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the lamp oracle scenes (scenes/lamps/) byte for byte.

The Light Falloff node on a mesh emitter's emission strength (quadratic, linear
and constant falloff with a smooth term) and in a surface shader's colour (the
camera ray length), and point, spot, area and sun lights whose emission shader
is not constant (Light Falloff, checker textures on the lamp's parametric
coordinates, UV and normal), visible to the camera and sampled with MIS, in the
sampling family's room, through the public C API.
Usage: python3 native/oracle/scenes/author_lamps.py [--check]."""
import argparse
import json
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'lamps'


def falloff_emitter(handle, falloff, strength, smooth, output):
    """An emission shader whose strength is the Light Falloff node's output."""
    return room.shader(handle, [{'handle': 'f', 'type': 'light_falloff', 'set': {'strength': strength, 'smooth': smooth}},
                                {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.85, 0.6]}}],
                       [['f', output, 'e', 'Strength'], ['e', 'Emission', 'output', 'Surface']])


def panel(handle, material, z):
    corners = [(-1.2, 2.4, z), (1.2, 2.4, z), (1.2, 2.4, z+1.6), (-1.2, 2.4, z+1.6)]
    return room.quad(handle, material, corners)


def scene(scene_id, description, falloff, smooth, surface=False):
    document = room.scene(scene_id, description, {}, samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_lamps.py (the sampling family room)'
    output = {'quadratic': 'Quadratic', 'linear': 'Linear', 'constant': 'Constant'}[falloff]
    nodes = document['nodes']
    nodes += [falloff_emitter('panel-emission', falloff, 6.0, smooth, output), *panel('panel', 'panel-emission', -0.6)]
    if surface:
        # The matte sphere's colour follows the camera ray length (Light Falloff on a surface shader).
        for node in nodes:
            if node.get('handle') == 'matte':
                node['graph'] = {'nodes': [{'handle': 'f', 'type': 'light_falloff', 'set': {'strength': 0.05, 'smooth': smooth}},
                                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.4}},
                                           {'handle': 'c', 'type': 'combine_color', 'set': {'g': 0.4, 'b': 0.6}}],
                                 'connect': [['f', output, 'c', 'Red'], ['c', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]}
    return document


def lamp_scene(scene_id, description, lamp, shader_nodes, connect, transform, extra=None):
    """The room without its sphere lamp: one lamp of the given kind with a node emission shader."""
    document = room.scene(scene_id, description, {}, samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_lamps.py (the sampling family room)'
    document['nodes'] = [n for n in document['nodes'] if n['handle'] not in ('lamp', 'lamp-object', 'emitter')]
    settings = {'strength': [1, 1, 1], 'use_mis': True, 'cast_shadow': True, 'max_bounces': 1024, 'is_enabled': True, 'normalize': True,
                'used_shaders': ['lamp-shader']}
    settings.update(extra or {})
    document['nodes'] += [room.shader('lamp-shader', shader_nodes, connect),
                          {'handle': 'lamp', 'create': lamp, 'set': settings},
                          {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': transform}}]
    return document


def checker(handle, vector_from, output, scale=3.0):
    return ([{'handle': 'g', 'type': 'geometry'},
             {'handle': 'c', 'type': 'checker_texture', 'set': {'scale': scale, 'color1': [1.0, 0.7, 0.3], 'color2': [0.2, 0.5, 1.0]}},
             {'handle': 'e', 'type': 'emission', 'set': {'strength': 1.0}}],
            [['g', vector_from, 'c', 'Vector'], ['c', 'Color', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']])


def falloff(strength, smooth, output):
    return ([{'handle': 'f', 'type': 'light_falloff', 'set': {'strength': strength, 'smooth': smooth}},
             {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.85, 0.6]}}],
            [['f', output, 'e', 'Strength'], ['e', 'Emission', 'output', 'Surface']])


# A small photometric file (IES LM-63-2002 text, written for these scenes): five vertical angles from 0 to 90
# degrees, three horizontal angles from 0 to 180 degrees (a bilaterally symmetric distribution).
IES_TEXT = """IESNA:LM-63-2002
[TEST] CodeBrix oracle scene distribution
TILT=NONE
1 1000 1 5 3 1 2 0 0 0
1 1 100
0 22.5 45 67.5 90
0 90 180
100 90 70 40 10
100 85 55 30 5
100 95 80 50 15
"""


def ies(strength):
    return ([{'handle': 'i', 'type': 'ies_light', 'set': {'ies': IES_TEXT, 'strength': strength}},
             {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.9, 0.75]}}],
            [['i', 'Fac', 'e', 'Strength'], ['e', 'Emission', 'output', 'Surface']])


def lamp_scenes():
    out = {}
    nodes, connect = ies(0.02)
    out['lamp-point-ies.json'] = lamp_scene('lamps/lamp-point-ies', 'Point light whose emission strength is an IES Texture node (incoming direction, bicubic table lookup)',
                                            'pointlight', nodes, connect, [1, 0, 0, 0.0, 0, 0, -1, 2.6, 0, 1, 0, 0.4],
                                            {'strength': [300, 300, 300], 'radius': 0.1, 'is_sphere': True})
    nodes, connect = falloff(1.0, 1.0, 'Linear')
    out['lamp-point-falloff.json'] = lamp_scene('lamps/lamp-point-falloff', 'Sphere point light whose emission strength is Light Falloff (linear, smooth 1)',
                                                'pointlight', nodes, connect, [1, 0, 0, 2.2, 0, 1, 0, 2.0, 0, 0, 1, -2.0],
                                                {'strength': [40, 34, 28], 'radius': 0.45, 'is_sphere': True})
    nodes, connect = checker('lamp-shader', 'Parametric', 'Color')
    out['lamp-spot-parametric.json'] = lamp_scene('lamps/lamp-spot-parametric', 'Spot light whose emission colour is a checker texture on the spot coordinates',
                                                  'spotlight', nodes, connect, [1, 0, 0, 0.8, 0, 0, 1, 2.8, 0, -1, 0, 0.2],
                                                  {'strength': [160, 160, 160], 'radius': 0.25, 'is_sphere': True, 'angle': 1.4, 'smooth': 0.3})
    out['lamp-area-parametric.json'] = lamp_scene('lamps/lamp-area-parametric', 'Rectangular area light whose emission colour is a checker texture on the area coordinates',
                                                  'arealight', nodes, connect, [1, 0, 0, 0.0, 0, 0, 1, 2.8, 0, -1, 0, 0.5],
                                                  {'strength': [80, 80, 80], 'sizeu': 2.4, 'sizev': 1.6, 'ellipse': False, 'spread': 3.14159265})
    out['lamp-point-normal.json'] = lamp_scene('lamps/lamp-point-normal', 'Sphere point light whose emission colour is a checker texture on its normal (sphere coordinates)',
                                               'pointlight', *checker('lamp-shader', 'Normal', 'Color'), [1, 0, 0, 1.8, 0, 1, 0, 1.6, 0, 0, 1, -2.4],
                                               {'strength': [60, 60, 60], 'radius': 0.6, 'is_sphere': True})
    out['lamp-sun-parametric.json'] = lamp_scene('lamps/lamp-sun-parametric', 'Sun light (angle 0.3) whose emission colour is its sun disc coordinates',
                                                 'sunlight', [{'handle': 'g', 'type': 'geometry'}, {'handle': 'e', 'type': 'emission', 'set': {'strength': 1.0}}],
                                                 [['g', 'Parametric', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']], [1, 0, 0, 0, 0, -0.8, 0.6, 0, 0, -0.6, -0.8, 0],
                                                 {'strength': [3, 3, 3], 'angle': 0.3})
    # The UV of a lamp (the Texture Coordinate node's UV output, an Attribute node asking for the standard UV map): the
    # pinned kernel answers it with the lamp's parametric coordinates, (1 - u - v, u, 0).
    out['lamp-area-uv.json'] = lamp_scene('lamps/lamp-area-uv', 'Rectangular area light whose emission colour is a checker texture on the '
                                          "Texture Coordinate node's UV output (the lamp's parametric coordinates through the attribute lookup)",
                                          'arealight', [{'handle': 't', 'type': 'texture_coordinate'},
                                                        {'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 3.0, 'color1': [1.0, 0.7, 0.3], 'color2': [0.2, 0.5, 1.0]}},
                                                        {'handle': 'e', 'type': 'emission', 'set': {'strength': 1.0}}],
                                          [['t', 'UV', 'c', 'Vector'], ['c', 'Color', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']],
                                          [1, 0, 0, 0.0, 0, 0, 1, 2.8, 0, -1, 0, 0.5],
                                          {'strength': [80, 80, 80], 'sizeu': 2.4, 'sizev': 1.6, 'ellipse': False, 'spread': 3.14159265})
    # An area light of zero size: Light::has_contribution disables it on the host (no contribution), next to the
    # room's sphere lamp.
    document = room.scene('lamps/area-zero-size', 'An area light of size zero (disabled by the host) beside the sphere lamp', {}, samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_lamps.py (the sampling family room)'
    document['nodes'] += [room.shader('zero-shader', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}],
                                      [['e', 'Emission', 'output', 'Surface']]),
                          {'handle': 'zero', 'create': 'arealight', 'set': {'strength': [80, 80, 80], 'use_mis': True, 'cast_shadow': True,
                                                                           'max_bounces': 1024, 'is_enabled': True, 'normalize': True,
                                                                           'used_shaders': ['zero-shader'], 'sizeu': 0.0, 'sizev': 1.0}},
                          {'handle': 'zero-object', 'create': 'object', 'set': {'geometry': 'zero', 'tfm': [1, 0, 0, 0.0, 0, 0, 1, 2.8, 0, -1, 0, 0.5]}}]
    out['area-zero-size.json'] = document
    return out


def scenes():
    return {**lamp_scenes(), 'light-falloff-quadratic.json': scene('lamps/light-falloff-quadratic', 'Light Falloff (quadratic, smooth 0.5) on a mesh emitter', 'quadratic', 0.5),
            'light-falloff-linear.json': scene('lamps/light-falloff-linear', 'Light Falloff (linear, smooth 0) on a mesh emitter and on a surface colour', 'linear', 0.0, True),
            'light-falloff-constant.json': scene('lamps/light-falloff-constant', 'Light Falloff (constant, smooth 2) on a mesh emitter and on a surface colour', 'constant', 2.0, True)}


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
