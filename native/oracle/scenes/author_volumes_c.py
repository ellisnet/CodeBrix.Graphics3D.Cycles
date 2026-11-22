#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the third set of volume oracle scenes (scenes/volumes/) byte for byte.

The heterogeneous box inside a world volume, with no background light object: the host supplies the background light
that pinned Cycles builds the world volume's octree from (scene/volume.cpp, VolumeManager::initialize_octree); a
heterogeneous world volume alone and with the heterogeneous box inside it; a heterogeneous world volume under a sun light,
whose shadow rays end in an octree segment without an end. Two Henyey-Greenstein lobes of different
anisotropy in one medium lit by the world (the phase-sampled path stores the pdf of all the phase closures).
Two overlapping heterogeneous boxes that are not closed meshes.
A dispersive glass sphere inside the homogeneous medium box.
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


def stepped_world(document):
    """The room's world volume with a piecewise constant density, 0.03 * (1 + (x > 0)) * (y < 1e36): heterogeneous inside the
    scene (two values) and still dense far outside it, up to a height no real scene has. A shadow ray to the sun leaves the
    octree and ends in a segment without an end, whose step count the pinned kernel takes from a float-to-int conversion
    of an out-of-range value: only comparisons and products, so the far samples are exact on every device."""
    world = next(n for n in document['nodes'] if n['handle'] == 'world')['graph']
    world['nodes'] += [{'handle': 'geo', 'type': 'geometry', 'set': {}}, {'handle': 'sep', 'type': 'separate_xyz', 'set': {}},
                       {'handle': 'below', 'type': 'math', 'set': {'math_type': 'less_than', 'value2': 1e36}},
                       {'handle': 'right', 'type': 'math', 'set': {'math_type': 'greater_than', 'value2': 0.0}},
                       {'handle': 'levels', 'type': 'math', 'set': {'math_type': 'add', 'value2': 1.0}},
                       {'handle': 'mask', 'type': 'math', 'set': {'math_type': 'multiply'}},
                       {'handle': 'wm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.03}}]
    world['connect'] += [['geo', 'Position', 'sep', 'Vector'], ['sep', 'Y', 'below', 'Value1'], ['sep', 'X', 'right', 'Value1'],
                         ['right', 'Value', 'levels', 'Value1'], ['levels', 'Value', 'mask', 'Value1'], ['below', 'Value', 'mask', 'Value2'],
                         ['mask', 'Value', 'wm', 'Value1'], ['wm', 'Value', 'wv', 'Density']]
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


# Two boxes of the heterogeneous medium with no back face (the face away from the camera), the second starting inside the
# first: both octrees have the plane x = 0 as a cell boundary.
OPEN_BOX_INDICES = [0, 2, 1, 1, 2, 3, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7]
OPEN_BOXES = (('boxa', (-1.6, 1.6, -1.9, 1.4, -1.4, 0.5)), ('boxb', (-1.0, 1.0, -1.0, 1.0, -0.5, 2.2)))


def open_boxes(document):
    """The family scene without its room and its box: two open boxes of the medium in front of the world colour, the sun
    behind the camera. A path leaves through a missing face with its box still on the volume stack."""
    nodes = [n for n in document['nodes'] if n.get('create') not in ('mesh', 'object') or n['handle'] == 'sun-object']
    next(n for n in nodes if n['handle'] == 'sun-object')['set']['tfm'] = [1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0]
    for handle, (x0, x1, y0, y1, z0, z1) in OPEN_BOXES:
        positions = [c for z in (z0, z1) for y in (y0, y1) for x in (x0, x1) for c in (x, y, z)]
        nodes += [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': OPEN_BOX_INDICES}, 'set': {'used_shaders': ['medium']}},
                  {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]
    document['nodes'] = nodes
    return document


def dispersive_glass(document):
    """The family room's glass sphere (the one inside the medium box) as a dispersive Principled BSDF."""
    glass = next(n for n in document['nodes'] if n['handle'] == 'glass')['graph']
    glass['nodes'] = [{'handle': 'n', 'type': 'principled_bsdf', 'set': {
        'distribution': 'ggx', 'base_color': [0.95, 0.97, 1.0], 'roughness': 0.05, 'ior': 1.5, 'transmission_weight': 1.0,
        'transmission_dispersion_scale': 1.0, 'transmission_dispersion_abbe_number': 15.0}}]
    return document


def scenes():
    return {
        'dispersion-in-medium.json': dispersive_glass(volumes.scene(
            'volumes/dispersion-in-medium',
            'A dispersive Principled glass sphere (Transmission Dispersion Scale 1, Abbe number 15) inside the homogeneous '
            'medium box, point light with MIS: the monochromatic path scatters and is absorbed in the medium'))
        | {'sourceFixture': 'native/oracle/scenes/author_volumes_c.py (the medium box of author_volumes.py around a dispersive glass sphere)'},
        'heterogeneous-open-boxes.json': open_boxes(volumes.scene(
            'volumes/heterogeneous-open-boxes',
            'Two overlapping boxes of a heterogeneous medium (noise density) that have no back face, lit by a sun behind the '
            'camera, no room: paths miss with a box still on the volume stack, and both octrees share the plane x = 0, where '
            'the null-scattering walk depends on the last bits of the ray',
            lamp='sun', graph=volumes.noise_medium(2.4, 1.2))),
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
        'heterogeneous-world-sun-tail.json': stepped_world(volumes.scene(
            'volumes/heterogeneous-world-sun-tail',
            'A heterogeneous world volume with a piecewise constant density that stays dense far outside the scene, sun light: '
            'the shadow rays to the sun end in an octree segment without an end, where the step count of volume_transmittance '
            'is the oracle host\'s conversion of an out-of-range value (one step on x86-64)',
            lamp='sun', world_volume=True)),
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
