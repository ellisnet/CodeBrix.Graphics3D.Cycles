#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the Christensen-Burley subsurface oracle scenes (scenes/subsurface/, the files named below) byte for byte.

The sampling family's room of author_subsurface.py with the Burley method (disk sampling with up to four local hits):
the Principled BSDF's subsurface lobe, the Subsurface Scattering node next to a random walk sphere, the Sobol-Burley
pattern, and points in the scene and carrying a Burley closure, through the public C API.
Usage: python3 native/oracle/scenes/author_subsurface_disk.py [--check]."""
import argparse
import json

import author_points as points
import author_subsurface as subsurface

HERE = subsurface.HERE


def scene(scene_id, description, middle, right=None, integrator=None, samples=16, extra=()):
    """author_subsurface.scene with integrator settings and extra nodes appended."""
    document = subsurface.scene(scene_id, description, middle, right, samples=samples)
    document['sourceFixture'] = 'native/oracle/scenes/author_subsurface_disk.py (Christensen-Burley spheres in the sampling family room)'
    for entry in document['nodes']:
        if entry['handle'] == 'integrator':
            entry['set'].update(integrator or {})
    document['nodes'] = document['nodes']+list(extra)
    return document


def scenes():
    burley = subsurface.principled('sss', {'subsurface_method': 'burley'})
    return {
        'principled-burley.json': scene('subsurface/principled-burley', 'Principled BSDF subsurface with the Christensen-Burley method (middle); on the right half '
                                        'subsurface with a zero blue radius (a BSSRDF on two channels beside a diffuse closure)', burley,
                                        subsurface.principled('sss-half', {'subsurface_method': 'burley', 'subsurface_weight': 0.6, 'subsurface_radius': [1.0, 0.5, 0.0],
                                                                           'base_color': [0.88, 0.8, 0.7]})),
        'node-burley.json': scene('subsurface/node-burley', 'The Subsurface Scattering node with the Christensen-Burley method (middle) beside a random walk node (right): '
                                  'disk and walk paths in one subsurface queue', subsurface.node('sss', {'method': 'burley', 'scale': 0.6}),
                                  subsurface.node('sss-walk', {'subsurface_anisotropy': 0.3})),
        'burley-sobol.json': scene('subsurface/burley-sobol', 'Christensen-Burley subsurface (Principled BSDF) with the Sobol-Burley sampling pattern',
                                   subsurface.principled('sss', {'subsurface_method': 'burley', 'subsurface_radius': [1.2, 0.6, 0.3], 'subsurface_scale': 0.5}),
                                   integrator={'sampling_pattern': 'sobol_burley'}),
        'burley-beside-points.json': scene('subsurface/burley-beside-points', 'Christensen-Burley subsurface with points in the scene and on points: the disk probes '
                                           'ignore points, exit rays see them, a probe from a point finds no hit in its own object', burley,
                                           extra=[points.cloud('points', ['matte', 'sss'], extra={'shader': [i % 2 for i in range(40)]}, count=40,
                                                               center=(0.2, -1.0, -0.6), spread=0.9, radius=0.22), points.place('points-object', 'points')]),
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
