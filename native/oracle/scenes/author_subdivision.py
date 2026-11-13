#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the subdivision oracle scenes (scenes/subdivision/) byte for byte.

Linear subdivision with Cycles' adaptive dicing (cbc_host_subd_mesh, native/host/ABI.md "Geometry") in the camera
family's perspective room: a quad and pentagon cage (a quad patch and an n-gon split into quads) diced in pixel
space, and the same cage diced in object space with a vertex colour attribute on the cage.
Usage: python3 native/oracle/scenes/author_subdivision.py [--check]."""
import argparse
import json
from pathlib import Path

import author_cameras as cameras

HERE = Path(__file__).resolve().parent/'subdivision'
PERSPECTIVE = {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}
# A tilted quad and a pentagon beside it, in front of the room's back wall.
CAGE = {'positions': [-2.6, -1.4, 0.6, -0.4, -1.6, 0.2, -0.3, 1.2, -0.4, -2.5, 1.3, 0.0,
                      0.4, -1.3, 0.0, 2.4, -1.5, 0.5, 2.7, 0.4, 0.2, 1.6, 1.6, -0.3, 0.3, 0.6, 0.1],
        'faceCorners': [4, 5], 'corners': [0, 1, 2, 3, 4, 5, 6, 7, 8]}


def scene(name, description, subd, attributes=None, shader_nodes=None):
    document = cameras.scene('subdivision/'+name, description, dict(PERSPECTIVE))
    document['sourceFixture'] = 'native/oracle/scenes/author_subdivision.py (the sampling family room)'
    material = {'handle': 'cage-material', 'create': 'shader', 'graph': {'nodes': shader_nodes or [
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.45, 0.3], 'roughness': 0.2}}],
        'connect': [['d', 'BSDF', 'output', 'Surface']] if not shader_nodes else [['c', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]}}
    cage = dict(CAGE)
    if attributes:
        cage['attributes'] = attributes
    mesh = {'handle': 'cage', 'create': 'mesh', 'subdivision': cage,
            'set': {'used_shaders': ['cage-material'], 'subdivision_type': 'linear', **subd}}
    obj = {'handle': 'cage-object', 'create': 'object', 'set': {'geometry': 'cage'}}
    document['nodes'] += [material, mesh, obj]
    return document


def scenes():
    colours = [c for i in range(9) for c in ((0.2 + 0.08*i), 0.5, (0.9 - 0.08*i))]
    return {
        'linear-pixel.json': scene('linear-pixel', 'Linear subdivision of a quad and a pentagon cage, adaptive dicing in pixel space (rate 1, at most 6 levels)',
                                   {'subd_adaptive_space': 'pixel', 'subd_dicing_rate': 1.0, 'subd_max_level': 6}),
        'linear-object-attribute.json': scene(
            'linear-object-attribute', 'Linear subdivision of the same cage diced in object space (rate 0.25) with a vertex colour attribute on the cage',
            {'subd_adaptive_space': 'object', 'subd_dicing_rate': 0.25, 'subd_max_level': 6},
            attributes=[{'name': 'tint', 'value': 'color', 'domain': 'vertex', 'flags': ['subdivision'], 'data': colours}],
            shader_nodes=[{'handle': 'c', 'type': 'attribute', 'set': {'attribute': 'tint'}},
                          {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}}]),
    }


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
