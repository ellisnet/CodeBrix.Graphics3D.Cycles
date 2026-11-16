#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the bake scenes of a subdivision surface (scenes/bake/subdivision-*.json) byte for byte.

The sampling family room of author_bake.py with the bake target 'ball' replaced by a linear subdivision cage
(cbc_host_subd_mesh, native/host/ABI.md "Geometry" and "Baking"): the tilted quad and the pentagon of
author_subdivision.py, scaled into the sphere's place (the quad smooth, the pentagon flat), with a corner UV layout on the cage (a subdivision
attribute; each face an island) and diced in object space, the space a bake can dice in. The host bakes the triangles
Cycles dices from the cage. Combined, Diffuse Color, Normal and Position, and Combined, Normal and Position with true
displacement (a wave in the UV layout) on the diced mesh.
Usage: python3 native/oracle/scenes/author_bake_subdivision.py [--check]."""
import argparse
import json
from pathlib import Path

import author_bake as bake
import author_sampling as room
import author_subdivision as subdivision

HERE = Path(__file__).resolve().parent/'bake'
f32 = room.f32
CENTER, SCALE = (0.0, -0.9, 0.4), 0.42


def cage():
    """The subdivision family's cage in the bake sphere's place, with a corner UV layout of each face's own shape."""
    source = subdivision.CAGE
    xyz = [f32(CENTER[k % 3]+SCALE*v) for k, v in enumerate(source['positions'])]
    uv, start = [], 0
    faces = source['faceCorners']
    for face, count in enumerate(faces):
        own = source['corners'][start:start+count]
        xs, ys = [xyz[3*i] for i in own], [xyz[3*i+1] for i in own]
        width = 0.94/len(faces)
        for i in own:
            uv += [f32(0.03+face*width+(width-0.03)*(xyz[3*i]-min(xs))/(max(xs)-min(xs))), f32(0.04+0.9*(xyz[3*i+1]-min(ys))/(max(ys)-min(ys)))]
        start += count
    return {'handle': 'ball', 'create': 'mesh',
            'subdivision': {'positions': xyz, 'faceCorners': list(faces), 'corners': list(source['corners']),
                            'attributes': [{'name': 'uv', 'value': 'point2', 'domain': 'corner', 'flags': ['standard', 'subdivision'], 'data': uv}]},
            'set': {'used_shaders': ['textured'], 'subdivision_type': 'linear', 'subd_adaptive_space': 'object', 'subd_dicing_rate': 0.12,
                    'subd_max_level': 6, 'subd_smooth': [True]+[False]*(len(faces)-1)}}


def displaced(material):
    """The bake family's textured material with true displacement by a wave in the UV layout."""
    graph = material['graph']
    graph['nodes'] += [{'handle': 'w', 'type': 'wave_texture', 'set': {'scale': 3.0}},
                       {'handle': 'h', 'type': 'displacement', 'set': {'midlevel': 0.5, 'scale': 0.1}}]
    graph['connect'] += [['t', 'UV', 'w', 'Vector'], ['w', 'Fac', 'h', 'Height'], ['h', 'Displacement', 'output', 'Displacement']]
    material['set']['displacement_method'] = 'true'
    return material


def subdivision_scene(kind, displace):
    name = 'subdivision-'+('displaced-' if displace else '')+kind.replace('_', '-')
    what = 'The '+kind+' bake of a linear subdivision surface (a quad and a pentagon cage diced in object space)'
    document = bake.bake_scene('bake/'+name, what+(' with true displacement on the diced mesh' if displace else ''), {'type': kind})
    document['sourceFixture'] = 'native/oracle/scenes/author_bake_subdivision.py (bakes in the sampling family room)'
    nodes = []
    for n in document['nodes']:
        if n['handle'] == 'ball':
            n = cage()
        elif n['handle'] == 'textured' and displace:
            n = displaced(n)
        nodes.append(n)
    document['nodes'] = nodes
    return name+'.json', document


def scenes():
    out = {}
    for kind, displace in [('combined', False), ('diffuse_color', False), ('normal', False), ('position', False),
                           ('combined', True), ('normal', True), ('position', True)]:
        name, document = subdivision_scene(kind, displace)
        out[name] = document
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
