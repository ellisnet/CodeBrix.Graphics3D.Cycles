#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the third set of volume oracle scenes (scenes/volumes/) byte for byte.

The heterogeneous box inside a world volume, with no background light object: the host supplies the background light
that pinned Cycles builds the world volume's octree from (scene/volume.cpp, VolumeManager::initialize_octree); a
heterogeneous world volume alone and with the heterogeneous box inside it.
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


def scenes():
    return {
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
