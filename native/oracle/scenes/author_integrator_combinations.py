#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the integrator-combination oracle scenes byte for byte: shadow caustics (MNEE) with
fast GI and with motion blur on the receivers and a deforming caster (scenes/mnee/), and fast GI with transparent
shadows through a moving pane and a deforming centre quad (scenes/combinations/).

Each scene is derived from a committed scene, so the CPU kernels judge the WGSL device on the
combination end to end (compare.py). Usage: python3 native/oracle/scenes/author_integrator_combinations.py [--check]."""
import argparse
import copy
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHUTTER = 0.6


def f32(v):
    return round(struct.unpack('<f', struct.pack('<f', v))[0], 7)


def load(name):
    return json.loads((HERE/name).read_text())


def node(doc, handle):
    return next(n for n in doc['nodes'] if n['handle'] == handle)


def integrator(doc):
    return next(n for n in doc['nodes'] if n.get('scene') == 'integrator')['set']


def derive(doc, identifier, description):
    doc = copy.deepcopy(doc)
    doc['description'] = description+' (derived from '+doc['id']+')'
    doc['sourceFixture'] = 'native/oracle/scenes/'+doc['id']+'.json, '+doc['sourceFixture']
    doc['id'] = identifier
    return doc


def motion_blur(doc):
    integrator(doc)['motion_blur'] = True
    node(doc, 'camera')['set']['shuttertime'] = SHUTTER


def frames(steps, angle, offset):
    """Object motion: rotate about +Y by angle*u and translate by offset*u, u from -1 to 1
    over the steps (the centre step is the identity), row-major 3x4."""
    out = []
    for i in range(steps):
        u = 2.0*i/(steps-1)-1.0
        c, s = math.cos(angle*u), math.sin(angle*u)
        rows = [[c, 0.0, s, offset[0]*u], [0.0, 1.0, 0.0, offset[1]*u], [-s, 0.0, c, offset[2]*u]]
        out.append([f32(v) for row in rows for v in row])
    return out


def deform(mesh, steps):
    """Deformation motion: the authored displacement of native/cycles/deforming_motion_fixture.h,
    every step in time order (the centre step is the mesh)."""
    centre = mesh['positions']
    positions = []
    for step in range(steps):
        u = 2.0*step/(steps-1)-1.0
        for k in range(0, len(centre), 3):
            x, y, z = centre[k:k+3]
            positions += [f32(x+0.08*u*y), f32(y+0.04*u*x), f32(z+0.13*u*x*y+0.035*u*u*x)]
    mesh['motion'] = {'steps': steps, 'positions': positions}


def scenes():
    out = {}
    caustic = load('mnee/glass-sphere-caustic.json')
    doc = derive(caustic, 'mnee/glass-sphere-fast-gi', 'MNEE shadow caustics with fast GI in its additive mode '
                 '(KERNEL_FEATURE_AO_ADDITIVE: an AO shadow path beside the MNEE shadow path; mnee_fixture.h case 5)')
    integrator(doc).update(ao_bounces=1, ao_factor=0.5, ao_distance=1.2, ao_additive_factor=0.7)
    out['mnee/glass-sphere-fast-gi.json'] = doc
    doc = derive(caustic, 'mnee/glass-sphere-object-motion', 'MNEE shadow caustics with object motion blur: the floor and wall '
                 'receivers move under the static glass caster (mnee_fixture.h case 6; moving casters are refused)')
    motion_blur(doc)
    node(doc, 'floor-object')['set']['motion'] = frames(5, 0.05, (0.2, 0.0, 0.1))
    node(doc, 'wall-object')['set']['motion'] = frames(3, 0.04, (0.15, 0.05, 0.0))
    out['mnee/glass-sphere-object-motion.json'] = doc
    doc = derive(caustic, 'mnee/glass-sphere-deforming', 'MNEE shadow caustics with deformation motion blur: the glass '
                 'caster sphere and the floor receiver deform; the pinned walk takes a deforming caster\'s centre-frame corners '
                 '(mnee_fixture.h cases 7 and 8)')
    motion_blur(doc)
    deform(node(doc, 'sphere')['mesh'], 3)
    deform(node(doc, 'floor')['mesh'], 5)
    out['mnee/glass-sphere-deforming.json'] = doc
    transparent = load('combinations/replace-transparent-fast-gi.json')
    doc = derive(transparent, 'combinations/moving-transparent-fast-gi', 'fast GI (replace mode) with transparent '
                 'shadows through a moving transparent pane and a deforming centre quad, motion blur on '
                 '(AO and light shadow rays through moving transparency)')
    motion_blur(doc)
    node(doc, 'pane-object')['set']['motion'] = frames(3, 0.12, (0.3, 0.05, 0.1))
    deform(node(doc, 'center')['mesh'], 3)
    out['combinations/moving-transparent-fast-gi.json'] = doc
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    for name, document in scenes().items():
        text = json.dumps(document, indent=1)+'\n'
        path = HERE/name
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+name)
        else:
            path.write_text(text)
    print('checked' if args.check else 'written', len(scenes()), 'scenes')


if __name__ == '__main__':
    main()
