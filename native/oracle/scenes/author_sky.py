#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the sky oracle scenes (scenes/sky/) byte for byte.

The Sky Texture node as the world (Preetham, Hosek-Wilkie, single and multiple
scattering) lighting the sampling family's room through a background light
(map importance sampling with MIS), through the public C API. The sun disc
scene feeds the node through a Vector Math node, so the host samples the sun
from the importance map instead of guiding towards it; the sun-guiding scenes
leave the Generated direction unlinked, so the background light also samples
the sun's cone (sun guiding).
Usage: python3 native/oracle/scenes/author_sky.py [--check]."""
import argparse
import json
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'sky'


def sky_world(values, strength, normalized_vector=False):
    """The world shader: a Sky Texture node into a Background shader (emission sampling auto)."""
    nodes = [{'handle': 'sky', 'type': 'sky_texture', 'set': values},
             {'handle': 'background', 'type': 'background_shader', 'set': {'strength': strength}}]
    connect = [['sky', 'Color', 'background', 'Color'], ['background', 'Background', 'output', 'Surface']]
    if normalized_vector:
        nodes += [{'handle': 'tc', 'type': 'texture_coordinate'},
                  {'handle': 'unit', 'type': 'vector_math', 'set': {'math_type': 'normalize'}}]
        connect += [['tc', 'Generated', 'unit', 'Vector1'], ['unit', 'Vector', 'sky', 'Vector']]
    return {'handle': 'world', 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect},
            'set': {'emission_sampling_method': 'auto'}}


def scene(scene_id, description, values, strength, normalized_vector=False):
    document = room.scene(scene_id, description, {}, samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_sky.py (the sampling family room under a sky)'
    nodes = []
    for entry in document['nodes']:
        if entry['handle'] == 'world':
            nodes.append(sky_world(values, strength, normalized_vector))
            # Standalone Cycles samples the world only through a background light object.
            nodes.append({'handle': 'world-light', 'create': 'backgroundlight',
                          'set': {'use_mis': True, 'map_resolution': 256, 'strength': [1.0, 1.0, 1.0], 'used_shaders': ['world']}})
            nodes.append({'handle': 'world-light-object', 'create': 'object', 'set': {'geometry': 'world-light'}})
            continue
        nodes.append(entry)
    document['nodes'] = nodes
    return document


def scenes():
    scattering = {'sun_elevation': 0.35, 'sun_rotation': 1.1, 'altitude': 400.0, 'air_density': 1.2,
                  'aerosol_density': 0.8, 'ozone_density': 1.0}
    return {
        'preetham.json': scene('sky/preetham', 'Preetham sky (turbidity 2.5) as the world', {
            'sky_type': 'preetham', 'sun_disc': False, 'sun_direction': [0.35, -0.55, 0.76], 'turbidity': 2.5}, 0.05),
        'hosek-wilkie.json': scene('sky/hosek-wilkie', 'Hosek-Wilkie sky (turbidity 3.5, ground albedo 0.4) as the world', {
            'sky_type': 'hosek_wilkie', 'sun_disc': False, 'sun_direction': [-0.4, -0.3, 0.87], 'turbidity': 3.5, 'ground_albedo': 0.4}, 0.05),
        'single-scattering.json': scene('sky/single-scattering', 'Single-scattering sky without the sun disc as the world', {
            'sky_type': 'single_scattering', 'sun_disc': False, **scattering}, 1.0),
        'multiple-scattering.json': scene('sky/multiple-scattering', 'Multiple-scattering sky without the sun disc as the world', {
            'sky_type': 'multiple_scattering', 'sun_disc': False, **scattering}, 1.0),
        'multiple-scattering-sun.json': scene('sky/multiple-scattering-sun', 'Multiple-scattering sky with a large sun disc (limb darkening), '
                                              'the direction through a Vector Math node (map sampling, no sun guiding)', {
            'sky_type': 'multiple_scattering', 'sun_disc': True, 'sun_size': 0.35, 'sun_intensity': 0.05, **scattering}, 1.0, True),
        'multiple-scattering-sun-guiding.json': scene('sky/multiple-scattering-sun-guiding', 'Multiple-scattering sky with the sun disc on the '
                                                      'Generated direction: the background light guides samples towards the sun (sun and map strategies with MIS)', {
            'sky_type': 'multiple_scattering', 'sun_disc': True, 'sun_size': 0.2, 'sun_intensity': 0.2, **scattering}, 1.0),
        'single-scattering-sun-guiding.json': scene('sky/single-scattering-sun-guiding', 'Single-scattering sky with a small sun disc and sun guiding', {
            'sky_type': 'single_scattering', 'sun_disc': True, 'sun_size': 0.05, 'sun_intensity': 1.0, 'sun_elevation': 0.6, 'sun_rotation': 2.4,
            'altitude': 100.0, 'air_density': 1.0, 'aerosol_density': 1.0, 'ozone_density': 1.0}, 1.0),
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
