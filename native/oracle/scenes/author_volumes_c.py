#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the third set of volume oracle scenes (scenes/volumes/) byte for byte.

The heterogeneous box inside a world volume, with no background light object: the host supplies the background light
that pinned Cycles builds the world volume's octree from (scene/volume.cpp, VolumeManager::initialize_octree); a
heterogeneous world volume alone and with the heterogeneous box inside it. Two Henyey-Greenstein lobes of different
anisotropy in one medium lit by the world (the phase-sampled path stores the pdf of all the phase closures).
Usage: python3 native/oracle/scenes/author_volumes_c.py [--check]."""
import argparse
import json
from pathlib import Path

import author_volumes as volumes

HERE = Path(__file__).resolve().parent/'volumes'


def heterogeneous_world(document):
    """The room's world volume (scatter density 0.03) with its density from a Noise Texture: 0.03 * Fac, at the shading
    position (a world volume has no object space of its own)."""
    world = next(n for n in document['nodes'] if n['handle'] == 'world')['graph']
    world['nodes'] += [{'handle': 'wn', 'type': 'noise_texture', 'set': {'scale': 0.5, 'detail': 1.0}},
                       {'handle': 'wm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.03}}]
    world['connect'] += [['wn', 'Fac', 'wm', 'Value1'], ['wm', 'Value', 'wv', 'Density']]
    return document


# Two scattering lobes of different anisotropy and density in one shader (two phase closures, different sample weights).
MIXED_PHASES = ([{'handle': 'a', 'type': 'absorption_volume', 'set': {'density': 0.05, 'color': [0.8, 0.65, 0.4]}},
                 {'handle': 's', 'type': 'scatter_volume', 'set': {'density': 0.3, 'color': [0.75, 0.8, 0.9], 'anisotropy': 0.75}},
                 {'handle': 'b', 'type': 'scatter_volume', 'set': {'density': 0.15, 'color': [0.9, 0.8, 0.7], 'anisotropy': -0.5}},
                 {'handle': 'add', 'type': 'add_closure'}, {'handle': 'add2', 'type': 'add_closure'}],
                [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'add2', 'Closure1'], ['b', 'Volume', 'add2', 'Closure2'],
                 ['add2', 'Closure', 'output', 'Volume']])


def gradient_world(document):
    """A linear gradient over the world direction as the background colour (a constant world leaves the background light
    disabled, and its forward MIS with it)."""
    world = next(n for n in document['nodes'] if n['handle'] == 'world')
    world['graph'] = {'nodes': [{'handle': 'sky', 'type': 'gradient_texture', 'set': {'gradient_type': 'linear'}},
                                {'handle': 'coordinates', 'type': 'texture_coordinate'},
                                {'handle': 'b', 'type': 'background_shader', 'set': {'strength': 2.0}}],
                      'connect': [['coordinates', 'Generated', 'sky', 'Vector'], ['sky', 'Color', 'b', 'Color'], ['b', 'Background', 'output', 'Surface']]}
    return document


def scenes():
    return {
        'mixed-phase-world.json': gradient_world(volumes.scene(
            'volumes/mixed-phase-world',
            'The medium box with two Henyey-Greenstein lobes (anisotropy 0.75 and -0.5) lit only by a gradient world through a '
            'background light with MIS: the phase-sampled path stores the pdf of both phase closures for the forward MIS weight',
            lamp='none', world_light=True, graph=MIXED_PHASES)),
        'heterogeneous-world-volume.json': heterogeneous_world(volumes.scene(
            'volumes/heterogeneous-world-volume',
            'A heterogeneous world volume (scatter density 0.03 times a Noise Texture) around the homogeneous box, point light '
            'with MIS, no background light object: the world entry integrated by null scattering through the octree rooted '
            'at the background light the host supplies',
            world_volume=True)),
        'heterogeneous-in-heterogeneous-world.json': heterogeneous_world(volumes.scene(
            'volumes/heterogeneous-in-heterogeneous-world',
            'The heterogeneous box (noise density) inside the heterogeneous world volume, point light with MIS: two '
            'heterogeneous stack entries, the world\'s and the box\'s',
            world_volume=True, graph=volumes.noise_medium())),
        'heterogeneous-in-world-volume.json': volumes.scene(
            'volumes/heterogeneous-in-world-volume',
            'The heterogeneous box (noise density) inside a thin scattering world volume, point light with MIS, no background light object: '
            'the octree set-up of the shadow and scatter paths reads the world entry beside the heterogeneous one',
            world_volume=True, graph=volumes.noise_medium()),
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
