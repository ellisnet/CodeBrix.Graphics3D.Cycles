#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the subsurface scenes with caustic spot, area and background lights and with instances (scenes/subsurface/) byte for byte.

scenes/subsurface/mnee-receivers (a glass caustic caster, Christensen-Burley and random walk spheres and the floor as caustic
receivers) with the caustic point lamp replaced by a spot light or an area light above the caster, or with a background light
with caustics (an importance-mapped gradient world) beside the point lamp. scenes/subsurface/walk-beside-instances: the
subsurface spheres (single-user meshes, so their transform is applied) beside two instances of one box: the random walk's
local traversal from a transform-applied object starts at the scene root and meets the instance leaves.
Usage: python3 native/oracle/scenes/author_subsurface_caustic_lights.py [--check]."""
import argparse
import copy
import json
from pathlib import Path

import author_subsurface as subsurface
import author_subsurface_combinations as combinations

HERE = subsurface.HERE
SOURCE = 'native/oracle/scenes/author_subsurface_caustic_lights.py (subsurface spheres in the sampling family room, with {})'
# Local -Z to world -Y (author_mnee.py DOWN).
DOWN = lambda x, y, z: [1, 0, 0, x, 0, 0, 1, y, 0, -1, 0, z]


def receivers(scene_id, description, what):
    document = copy.deepcopy(combinations.scenes()['mnee-receivers.json'])
    document['id'] = scene_id
    document['description'] = description
    document['sourceFixture'] = SOURCE.format(what)
    return document


def node(document, handle):
    return next(n for n in document['nodes'] if n['handle'] == handle)


def lamp(document, kind, settings, tfm):
    light = node(document, 'lamp')
    light['create'] = kind
    light['set'] = {k: v for k, v in light['set'].items() if k not in ('radius', 'is_sphere')} | settings
    node(document, 'lamp-object')['set']['tfm'] = tfm
    return document


def background(document):
    node(document, 'world')['graph'] = {'nodes': [{'handle': 'sky', 'type': 'gradient_texture', 'set': {'gradient_type': 'linear'}},
                                                  {'handle': 'coordinates', 'type': 'texture_coordinate'},
                                                  {'handle': 'b', 'type': 'background_shader', 'set': {'strength': 1.5}}],
                                        'connect': [['coordinates', 'Generated', 'sky', 'Vector'], ['sky', 'Color', 'b', 'Color'], ['b', 'Background', 'output', 'Surface']]}
    at = document['nodes'].index(node(document, 'bg'))+1
    document['nodes'][at:at] = [{'handle': 'world-light', 'create': 'backgroundlight', 'set': {'use_mis': True, 'use_caustics': True, 'map_resolution': 64,
                                                                                                 'strength': [1.0, 1.0, 1.0], 'used_shaders': ['world']}},
                                {'handle': 'world-light-object', 'create': 'object', 'set': {'geometry': 'world-light'}}]
    return document


def beside_instances():
    document = subsurface.scene('subsurface/walk-beside-instances', 'Random walk and Christensen-Burley spheres (transform-applied) beside two instances of one box: '
                                'the walk\'s local traversal from the scene root meets instance leaves',
                                subsurface.node('sss', {'method': 'burley', 'scale': 2.0}), subsurface.node('sss-walk', {'scale': 2.5, 'subsurface_anisotropy': 0.3}))
    document['sourceFixture'] = SOURCE.format('two instances of one box')
    positions = [c for k in range(8) for c in ((0.5 if k & 1 else -0.5), (0.5 if k & 2 else -0.5), (0.5 if k & 4 else -0.5))]
    indices = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7]
    document['nodes'] += [{'handle': 'shared', 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices}, 'set': {'used_shaders': ['wall']}},
                          # Across the front of the Christensen-Burley sphere, and inside the random walk sphere (the walk's rays cross its box).
                          {'handle': 'shared-a', 'create': 'object', 'set': {'geometry': 'shared', 'tfm': [0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, -0.9, 0.0, 0.0, 0.5, -0.5]}},
                          {'handle': 'shared-b', 'create': 'object', 'set': {'geometry': 'shared', 'tfm': [0.5, 0.0, 0.3, 2.3, 0.0, 0.5, 0.0, -0.9, -0.3, 0.0, 0.5, 0.4]}}]
    return document


def scenes():
    return {
        'mnee-receivers-spot.json': lamp(receivers('subsurface/mnee-receivers-spot', 'MNEE caustics from a spot light (disk radius 0.25) above the glass caster onto the '
                                                   'Christensen-Burley and random walk receivers', 'MNEE caustics from a spot light'),
                                         'spotlight', {'radius': 0.25, 'is_sphere': False, 'angle': 1.3, 'smooth': 0.3}, DOWN(-1.2, 2.4, 0.4)),
        'mnee-receivers-area.json': lamp(receivers('subsurface/mnee-receivers-area', 'MNEE caustics from an area light (0.6 by 0.4) above the glass caster onto the '
                                                   'Christensen-Burley and random walk receivers', 'MNEE caustics from an area light'),
                                         'arealight', {'is_portal': False, 'sizeu': 0.6, 'sizev': 0.4}, DOWN(-1.2, 2.4, 0.4)),
        'mnee-receivers-background.json': background(receivers('subsurface/mnee-receivers-background', 'MNEE caustics from a background light with caustics (importance '
                                                               'map, a gradient world) beside the caustic point lamp, onto the subsurface receivers',
                                                               'MNEE caustics from a background light')),
        'walk-beside-instances.json': beside_instances(),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
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
