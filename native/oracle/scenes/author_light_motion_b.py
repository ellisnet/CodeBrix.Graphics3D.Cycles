#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the moving-lights oracle scenes (scenes/light-motion/moving-lamps*) byte for byte.

Object motion on light objects with motion blur: light-motion/camera-object-motion (a moving camera and a moving
object) with its point lamp given object motion, plus a moving spot lamp and a moving area lamp. ObjectManager
decomposes a light object's motion and sets SD_OBJECT_MOTION on it; KernelLight keeps one transform, so the lamps
light the scene from their authored transform, and a lamp's emission shader sees the motion through
shader_setup_from_sample's object transforms. moving-lamps-object-shader gives the spot and area lamps an emission
shader driven by object-space texture coordinates (non-constant emission), so the motion transform is read.
Usage: python3 native/oracle/scenes/author_light_motion_b.py [--check]."""
import argparse
import copy
import json
from pathlib import Path

import author_starters as s

HERE = Path(__file__).resolve().parent/'light-motion'
DOWN = lambda x, y, z: [1, 0, 0, x, 0, 0, 1, y, 0, -1, 0, z]


def shifted(tfm, dx, dy, dz):
    out = list(tfm)
    out[3] += dx
    out[7] += dy
    out[11] += dz
    return [round(v, 7) for v in out]


def object_shader(handle):
    """Emission whose colour is the object-space position (Texture Coordinate Object), so it depends on the object transform."""
    return {'handle': handle, 'create': 'shader', 'graph': {
        'nodes': [{'handle': 'tc', 'type': 'texture_coordinate', 'set': {}},
                  {'handle': 'sep', 'type': 'separate_xyz', 'set': {}},
                  {'handle': 'c', 'type': 'combine_color', 'set': {}},
                  {'handle': 'e', 'type': 'emission', 'set': {'strength': 1.0}}],
        'connect': [['tc', 'Object', 'sep', 'Vector'], ['sep', 'X', 'c', 'Red'], ['sep', 'Y', 'c', 'Green'], ['sep', 'Z', 'c', 'Blue'],
                    ['c', 'Color', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']]},
        'set': {'emission_sampling_method': 'none'}}


def moving_lamps(object_space):
    document = json.loads((HERE/'camera-object-motion.json').read_text())
    name = 'moving-lamps'+('-object-shader' if object_space else '')
    document['id'] = 'light-motion/'+name
    document['description'] = ('Object motion on light objects with motion blur: a moving point, spot and area lamp beside a moving object and a '
                               'moving camera (the lamps light from their authored transforms)' +
                               ('; the spot and area lamps have an emission shader of object-space coordinates' if object_space else ''))
    document['sourceFixture'] = 'native/oracle/scenes/light-motion/camera-object-motion.json (author_light_motion_combinations.py) with moving lamps'
    nodes = document['nodes']
    point = next(n for n in nodes if n['handle'] == 'light-0-object')
    point['set']['motion'] = [shifted(point['set']['tfm'], -0.3, 0.1, 0.0), list(point['set']['tfm']), shifted(point['set']['tfm'], 0.35, -0.05, 0.2)]
    spot = s.light('spot', 'spotlight', dict(radius=0.2, is_sphere=True, angle=1.0, smooth=0.3), DOWN(-1.2, 2.8, 0.6), [160, 160, 180])
    area = s.light('area', 'arealight', dict(sizeu=1.2, sizev=0.8, ellipse=False, spread=1.2, is_portal=False), DOWN(0.4, 2.4, 1.6), [60, 70, 90])
    for part in (spot, area):
        lamp_object = part[2]
        lamp_object['set']['motion'] = [shifted(lamp_object['set']['tfm'], 0.25, 0.0, -0.2), list(lamp_object['set']['tfm']),
                                        shifted(lamp_object['set']['tfm'], -0.2, 0.1, 0.3)]
        if object_space:
            part[0] = object_shader(part[0]['handle'])
        nodes += part
    return name+'.json', document


def scenes():
    return dict([moving_lamps(False), moving_lamps(True)])


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
