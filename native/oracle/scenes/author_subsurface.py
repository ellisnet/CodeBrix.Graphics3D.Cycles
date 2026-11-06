#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the subsurface scattering oracle scenes (scenes/subsurface/) byte for byte.

The sampling family's room with its spheres, where the middle and right spheres carry subsurface scattering
materials (Principled BSDF subsurface lobes and the Subsurface Scattering node with the random walk methods):
skin, wax and marble-like settings, a mixed BSSRDF and diffuse surface and a channel below the minimum radius,
through the public C API.
Usage: python3 native/oracle/scenes/author_subsurface.py [--check]."""
import argparse
import json
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'subsurface'


def principled(handle, values):
    settings = {'distribution': 'multi_ggx', 'base_color': [0.8, 0.55, 0.45], 'roughness': 0.35, 'ior': 1.45, 'subsurface_weight': 1.0,
                'subsurface_method': 'random_walk', 'subsurface_radius': [1.0, 0.45, 0.25], 'subsurface_scale': 0.3}
    settings.update(values)
    return room.shader(handle, [{'handle': 'p', 'type': 'principled_bsdf', 'set': settings}], [['p', 'BSDF', 'output', 'Surface']])


def node(handle, values):
    settings = {'method': 'random_walk', 'color': [0.85, 0.7, 0.55], 'scale': 0.4, 'radius': [1.0, 0.6, 0.35], 'subsurface_ior': 1.4,
                'subsurface_roughness': 1.0, 'subsurface_anisotropy': 0.0}
    settings.update(values)
    return room.shader(handle, [{'handle': 's', 'type': 'subsurface_scattering', 'set': settings}], [['s', 'BSSRDF', 'output', 'Surface']])


def scene(scene_id, description, middle, right=None, samples=16):
    """The room with the middle sphere's material replaced (and the right one's when given)."""
    document = room.scene(scene_id, description, {}, samples=samples)
    document['sourceFixture'] = 'native/oracle/scenes/author_subsurface.py (subsurface spheres in the sampling family room)'
    nodes = []
    for entry in document['nodes']:
        nodes.append(entry)
        if entry['handle'] == 'matte':
            nodes.append(middle)
            if right is not None:
                nodes.append(right)
    for entry in nodes:
        if entry['handle'] == 'sphere-1':
            entry['set']['used_shaders'] = [middle['handle']]
        if entry['handle'] == 'sphere-2' and right is not None:
            entry['set']['used_shaders'] = [right['handle']]
    document['nodes'] = nodes
    return document


def scenes():
    return {
        'principled-random-walk.json': scene('subsurface/principled-random-walk', 'Principled BSDF subsurface (random walk, van de Hulst mapping): a skin-toned sphere',
                                             principled('sss', {})),
        'principled-skin.json': scene('subsurface/principled-skin', 'Principled BSDF subsurface with the skin model (diffuse or refractive entry, the fitted albedo mapping)',
                                      principled('sss', {'subsurface_method': 'random_walk_skin', 'subsurface_ior': 1.38})),
        'principled-wax.json': scene('subsurface/principled-wax', 'Principled BSDF subsurface wax: forward-scattering anisotropy 0.6, a warm albedo, a larger radius',
                                     principled('sss', {'base_color': [0.9, 0.75, 0.45], 'subsurface_anisotropy': 0.6, 'subsurface_scale': 0.6, 'roughness': 0.25})),
        'principled-marble.json': scene('subsurface/principled-marble', 'Principled BSDF marble-like: half subsurface (a BSSRDF and a diffuse closure), near-white, '
                                        'backward anisotropy, and a scattering sphere beside it whose blue radius is zero (a diffuse closure for that channel)',
                                        principled('sss', {'base_color': [0.88, 0.87, 0.84], 'subsurface_weight': 0.5, 'subsurface_anisotropy': -0.3, 'roughness': 0.15,
                                                            'subsurface_radius': [0.9, 0.8, 0.7]}),
                                        principled('sss-zero', {'subsurface_radius': [1.0, 0.5, 0.0]})),
        'node-methods.json': scene('subsurface/node-methods', 'Subsurface Scattering nodes: the random walk with anisotropy (middle) and the legacy random walk (right)',
                                   node('sss', {'subsurface_anisotropy': 0.4}), node('sss-legacy', {'method': 'random_walk_legacy', 'subsurface_anisotropy': 0.2})),
        'node-skin.json': scene('subsurface/node-skin', 'The Subsurface Scattering node with the skin model and a rough refractive entry (roughness 0.5)',
                                node('sss', {'method': 'random_walk_skin', 'subsurface_roughness': 0.5})),
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
