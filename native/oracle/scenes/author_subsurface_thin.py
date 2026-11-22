#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the thin-walled Principled subsurface oracle scenes (scenes/subsurface/, the files named below) byte for byte.

The sampling family's room of author_subsurface.py with thin-walled Principled BSDF spheres whose subsurface lobe becomes
diffuse and translucent closures (bsdf_thin_subsurface_setup), or with a diffuse roughness Oren-Nayar and rough translucent
closures, weighted by the subsurface anisotropy; with transmission and a coat, and with the Christensen-Burley method
selected (a thin wall has no BSSRDF), through the public C API.
Usage: python3 native/oracle/scenes/author_subsurface_thin.py [--check]."""
import argparse
import json

import author_subsurface as subsurface

HERE = subsurface.HERE


def scene(scene_id, description, middle, right=None):
    document = subsurface.scene(scene_id, description, middle, right)
    document['sourceFixture'] = 'native/oracle/scenes/author_subsurface_thin.py (thin-walled subsurface spheres in the sampling family room)'
    return document


def thin(handle, values):
    settings = {'thin_wall': 1, 'subsurface_anisotropy': 0.3}
    settings.update(values)
    return subsurface.principled(handle, settings)


def thin_linked(handle, values):
    """Thin Wall from a checker (0 or 1): both of the Principled subsurface lobe's forms in one shader."""
    settings = {'distribution': 'multi_ggx', 'base_color': [0.8, 0.55, 0.45], 'roughness': 0.35, 'ior': 1.45, 'subsurface_weight': 0.8,
                'subsurface_method': 'random_walk', 'subsurface_radius': [1.0, 0.45, 0.25], 'subsurface_scale': 0.3, 'subsurface_anisotropy': 0.3}
    settings.update(values)
    return subsurface.room.shader(handle, [{'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 3.0, 'color1': [1, 1, 1], 'color2': [0, 0, 0]}},
                                           {'handle': 'p', 'type': 'principled_bsdf', 'set': settings}],
                                  [['c', 'Fac', 'p', 'Thin Wall'], ['p', 'BSDF', 'output', 'Surface']])


def scenes():
    return {
        'principled-thin-wall.json': scene('subsurface/principled-thin-wall', 'Thin-walled Principled BSDF subsurface: diffuse and translucent closures '
                                           '(anisotropy 0.3, middle); with a diffuse roughness of 0.6 and anisotropy -0.4, Oren-Nayar and rough translucent '
                                           'closures (right)', thin('sss', {}), thin('sss-rough', {'diffuse_roughness': 0.6, 'subsurface_anisotropy': -0.4})),
        'principled-thin-wall-mixed.json': scene('subsurface/principled-thin-wall-mixed', 'Thin-walled Principled BSDF with transmission 0.4, subsurface 0.6 '
                                                 'and a coat (middle); a thin wall with the Christensen-Burley method selected and a rough diffuse lobe (right)',
                                                 thin('sss', {'transmission_weight': 0.4, 'subsurface_weight': 0.6, 'coat_weight': 0.3, 'diffuse_roughness': 0.3,
                                                              'subsurface_anisotropy': 0.6}),
                                                 thin('sss-burley', {'subsurface_method': 'burley', 'subsurface_weight': 0.8, 'diffuse_roughness': 0.4,
                                                                     'subsurface_anisotropy': -0.2, 'base_color': [0.75, 0.6, 0.5]})),
        'principled-thin-wall-linked.json': scene('subsurface/principled-thin-wall-linked', 'A Principled BSDF with subsurface whose Thin Wall '
                                                  'is linked to a checker (thin-walled diffuse and translucent closures on one square, the BSSRDF on the '
                                                  'other; middle); the same with a diffuse roughness of 0.5 (right)',
                                                  thin_linked('sss', {}), thin_linked('sss-rough', {'diffuse_roughness': 0.5})),
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
